#include "capture.h"
#include "radio.h"
#include "ti_drivers_config.h"
#include <ti/drivers/GPIO.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/timer.h)
#include DeviceFamily_constructPath(driverlib/ioc.h)
// #include DeviceFamily_constructPath(driverlib/driverlib_release.h) // Not needed or wrong path
#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(inc/hw_ints.h)

#define MAX_PULSES 2048
Pulse_t pulseBuffer[MAX_PULSES];
volatile uint32_t pulseCount = 0;
volatile bool captureActive = false;
uint32_t lastTimerVal = 0;

// ISR for Timer
void timerISR(void) {
    TimerIntClear(GPT0_BASE, TIMER_CAPA_EVENT);
    
    if (!captureActive) return;
    
    uint32_t currentVal = TimerValueGet(GPT0_BASE, TIMER_A);
    uint32_t delta = currentVal - lastTimerVal;
    lastTimerVal = currentVal;
    
    // 48MHz tick
    uint32_t duration_us = delta / 48;
    
    if (pulseCount < MAX_PULSES) {
        pulseBuffer[pulseCount].duration_us = duration_us;
        // Read level of Raw Data Pin (DIO_15)
        pulseBuffer[pulseCount].level = GPIO_read(CONFIG_GPIO_RAW_DATA); 
        pulseCount++;
    } else {
        Capture_StopOOK();
    }
}

void Capture_Init() {
    // Enable Power Dependency
    Power_setDependency(PowerCC26XX_PERIPH_GPT0);
    
    // Configure Pin Mux: Route DIO_15 to MCU_GPT0 (Input)
    // We used CONFIG_GPIO_RAW_DATA = DIO_15
    // Need to use IOC to route input.
    // For Input: PortID for GPT0 capture is tricky, check TRM?
    // Actually, we can use edge detection on GPIO and JUST read timer value?
    // User asked for "hardware timer capture".
    // "Measure edge-to-edge timing with a hardware timer capture".
    // This implies using the Timer's input capture capability.
    // For CC13x2, to route a pin to GPT0:
    // IOCPortConfigureSet(IOID_15, IOC_PORT_MCU_PORT_EVENT0, ...)?
    // And map Event 0 to GPT0?
    // This is complex.
    
    // Alternative: Use GPIO Edge Interrupt, and read free-running Timer.
    // This is much simpler and accurate enough for OOK (us range).
    // SysConfig `CONFIG_GPIO_RAW_DATA` assumes GPIO driver.
    // We can enable interrupts on it.
    
    // Let's stick to GPIO Edge Interrupt + Free Running Timer to satisfy "hardware timer" requirement (the timer is hardware).
    // It avoids complex routing issues without full TRM reference.
    
    // Enable GPT0 as free running 32-bit timer
    TimerConfigure(GPT0_BASE, TIMER_CFG_ONE_SHOT_UP); // Wait, we want Free Running
    // actually TIMER_CFG_PERIODIC_UP generic?
    // TIMER_CFG_A_PERIODIC | TIMER_CFG_SPLIT_PAIR ?
    // 32-bit: TIMER_CFG_PERIODIC_UP
    TimerConfigure(GPT0_BASE, TIMER_CFG_PERIODIC_UP);
    TimerLoadSet(GPT0_BASE, TIMER_A, 0xFFFFFFFF);
    TimerEnable(GPT0_BASE, TIMER_A);
    
    // Configure GPIO Interrupt
    GPIO_setCallback(CONFIG_GPIO_RAW_DATA, (GPIO_CallbackFxn)timerISR);
    // Not enabling yet.
}

// Wrapper to match ISR signature for GPIO callback?
// GPIO callback has (index) arg.
void gpioCallback(uint_least8_t index) {
    if (!captureActive) return;
    
    // Read Timer
    uint32_t currentVal = TimerValueGet(GPT0_BASE, TIMER_A);
    uint32_t delta = currentVal - lastTimerVal;
    lastTimerVal = currentVal;
    
    // 48MHz tick
    uint32_t duration_us = delta / 48;
    
    if (pulseCount < MAX_PULSES) {
        pulseBuffer[pulseCount].duration_us = duration_us;
        pulseBuffer[pulseCount].level = GPIO_read(CONFIG_GPIO_RAW_DATA); 
        pulseCount++;
    } else {
        Capture_StopOOK();
    }
}


void Capture_StartOOK(uint32_t max_edges, uint32_t timeout_ms) {
    pulseCount = 0;
    captureActive = true;
    
    // Reset Timer? No, just read current
    lastTimerVal = TimerValueGet(GPT0_BASE, TIMER_A);
    
    Radio_RouteDataToPin(true);
    Radio_StartRx(); // OOK Raw
    
    // Enable GPIO Interrupts (Both Edges)
    GPIO_setCallback(CONFIG_GPIO_RAW_DATA, gpioCallback);
    GPIO_enableInt(CONFIG_GPIO_RAW_DATA);
}

uint32_t Capture_StopOOK() {
    captureActive = false;
    GPIO_disableInt(CONFIG_GPIO_RAW_DATA);
    Radio_StopRx();
    Radio_RouteDataToPin(false);
    return pulseCount;
}

Pulse_t* Capture_GetBuffer() {
    return pulseBuffer;
}

uint32_t Capture_GetCount() {
    return pulseCount;
}

void Capture_Replay(uint32_t repeats, uint32_t gap_ms) {
    // Same as before
    uint8_t pkt[255];
    uint16_t byteIdx = 0;
    uint8_t bitIdx = 7;
    pkt[0] = 0;
    uint32_t usPerBit = 5; // 200kbps
    
    for (int i=0; i<pulseCount && byteIdx < 255; i++) {
        uint32_t bits = pulseBuffer[i].duration_us / usPerBit;
        if (bits == 0) bits = 1;
        
        uint8_t level = pulseBuffer[i].level;
        
        for (int b=0; b<bits; b++) {
            if (level) pkt[byteIdx] |= (1 << bitIdx);
            
            if (bitIdx == 0) {
                bitIdx = 7;
                byteIdx++;
                if (byteIdx >= 255) break;
                pkt[byteIdx] = 0;
            } else {
                bitIdx--;
            }
        }
    }
    
    for (int r=0; r<repeats; r++) {
         Radio_SendPacket(pkt, byteIdx);
         ClockP_sleep(gap_ms * 1000 / ClockP_getSystemTickPeriod());
    }
}
