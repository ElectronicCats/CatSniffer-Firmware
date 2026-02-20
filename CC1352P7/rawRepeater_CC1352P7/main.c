#include <stdint.h>
#include <stddef.h>

#include <ti/drivers/Board.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/dpl/ClockP.h>

#include <ti/devices/cc13x2x7_cc26x2x7/driverlib/cpu.h>
#include "ti_drivers_config.h"
#include "radio.h"
#include "capture.h"
#include "uart_proto.h"
#include <NoRTOS.h>

// Externs for LED GPIOs
// ...

int main(void)
{
    // Initialize Board
    Board_init();
    GPIO_init();
    
    // Blink LED 3 times to indicate start (Verify DIO6)
    // CPUdelay(16000000) approx 1/3 second at 48MHz
    for (int i = 0; i < 3; i++) {
        GPIO_write(CONFIG_GPIO_LED_0, 1);
        CPUdelay(8000000); 
        GPIO_write(CONFIG_GPIO_LED_0, 0);
        CPUdelay(8000000); 
    }
    
    // Init Modules
    UART_Init();
    Radio_Init();
    // Capture_Init is called on demand or here
    Capture_Init();
    
    NoRTOS_start(); // Start NoRTOS framework enabling interrupts

    
    // Print Boot Message
    UART_Print("\r\n\r\n--- CC1352P7 Raw Repeater Booted ---\r\n");

    while (1) {
        // Process UART
        UART_Process();
        
        // Low Power Policy
        // Since NoRTOS, we must explicitly call Power_idle if we want to sleep
        // But we are polling UART.
        // Power_idleFunc(); 
        // For now, spin loop to ensure UART response is fast.
    }
    
    return 0;
}
