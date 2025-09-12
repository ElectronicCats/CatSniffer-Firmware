/*
 * Catsniffer Dual USB CDC-ACM Header
 * Eduardo Contreras @ Electronic Cats
 */

#ifndef CATSNIFFER_H
#define CATSNIFFER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// Ring buffer and command buffer sizes
#define RING_BUF_SIZE 1024
#define COMMAND_BUF_SIZE 256


// Helper macro to simplify GPIO setup
#define INIT_GPIO(name, flags) \
	const struct gpio_dt_spec name = GPIO_DT_SPEC_GET_OR(DT_ALIAS(name), gpios, {0}); \
	do { \
		if (!device_is_ready(name.port)) { \
			printk("Error: " #name " device not ready\n"); \
			return 1; \
		} \
		gpio_pin_configure_dt(&name, flags); \
	} while (0)

// Mode definitions
enum MODE {
    PASSTHROUGH = 0,   // CC1352 passthrough @ 921600 baud
    BOOT = 1,          // CC1352 bootloader @ 500000 baud
};

// Band definitions
enum BAND {
    GIG = 0,      // 2.4GHz CC1352
    SUBGIG_1 = 1, // Sub-GHz CC1352  
    SUBGIG_2 = 2  // LoRa SX1262
};

// Catsniffer state structure
typedef struct {
    uint8_t mode;
    uint8_t band;
    unsigned long led_interval;
    int64_t previous_millis;
    unsigned long baud;
    bool command_recognized;
    uint8_t command_counter;
    char command_data[COMMAND_BUF_SIZE];
    size_t command_data_len;
} catsniffer_t;

// Command recognition pattern
extern const uint8_t commandID[5];

// Global catsniffer instance
extern catsniffer_t catsniffer;

// Catsniffer command format
// Commands are wrapped in: ñÿ<command>ÿñ
// Example: ñÿ<boot>ÿñ to enter bootloader mode

// LoRa command format (CDC1):
// TX <hex_data>     - Send LoRa packet (e.g., "TX 48656C6C6F")
// RX [timeout_ms]   - Enter receive mode (e.g., "RX 5000" or "RX" for continuous)
// FREQ <frequency>  - Set frequency in Hz (e.g., "FREQ 868000000")
// SF <7-12>         - Set spreading factor (e.g., "SF 7")
// PWR <-9 to 22>    - Set TX power in dBm (e.g., "PWR 14")
// STATUS            - Get device status

// Function prototypes
static void  reset_cc1352(void);
static void  boot_mode_cc1352(void);
static void  change_baud(unsigned long new_baud);
static void  change_band(unsigned long new_band);
static void  change_mode(unsigned long new_mode);
static void process_command(char *cmd, size_t len);
static void  process_lora_command(char *cmd_line);

#endif /* CATSNIFFER_H */