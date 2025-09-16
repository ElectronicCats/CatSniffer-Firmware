/*
 * Complete Dual USB CDC-ACM Catsniffer Firmware
 * Eduardo Contreras @ Electronic Cats
 * 
 */
#include "catsniffer.h"
#include "sx1262_driver.h"

LOG_MODULE_REGISTER(catsniffer_main, LOG_LEVEL_INF);

#define CDC0_NODE DT_NODELABEL(cdc_acm_uart0)
#define CDC1_NODE DT_NODELABEL(cdc_acm_uart1)

// Device definitions
#define CC1352_UART  DEVICE_DT_GET(DT_CHOSEN(uart_cc1352))
#define CDC0_DEV  DEVICE_DT_GET(CDC0_NODE)
#define CDC1_DEV  DEVICE_DT_GET(CDC1_NODE)

// Communication buffers 
uint8_t ring_cc1352_to_usb[RING_BUF_SIZE];
uint8_t ring_usb_to_cc1352[RING_BUF_SIZE];
uint8_t ring_sx1262_to_usb[RING_BUF_SIZE];
uint8_t ring_usb_to_sx1262[RING_BUF_SIZE];

struct ring_buf rb_cc1352_to_usb;
struct ring_buf rb_usb_to_cc1352;
struct ring_buf rb_sx1262_to_usb;
struct ring_buf rb_usb_to_sx1262;

// Device references 
const struct device *uart_cc1352;
const struct device *cdc0_dev;
const struct device *cdc1_dev;

// Command recognition pattern
//B1 C3 BF 3C 62
const uint8_t commandID[4] = {0xB1, 0xC3, 0xBF, 0x3C};

// Global catsniffer instance  
catsniffer_t catsniffer = {0};

// GPIO for CC1352 control
static const struct gpio_dt_spec pin_reset = GPIO_DT_SPEC_GET(DT_ALIAS(pin_reset), gpios);
static const struct gpio_dt_spec pin_boot = GPIO_DT_SPEC_GET(DT_ALIAS(pin_boot), gpios);
// GPIO for LED control
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
// GPIO for RF switch
// static const struct gpio_dt_spec cjtag0 = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag0), gpios);
// static const struct gpio_dt_spec cjtag1 = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag1), gpios);
// static const struct gpio_dt_spec cjtag2 = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag2), gpios);
// static const struct gpio_dt_spec cjtag3 = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag3), gpios);

// GPIO for RF switch
static const struct gpio_dt_spec ctf1 = GPIO_DT_SPEC_GET(DT_ALIAS(ctf1), gpios);
static const struct gpio_dt_spec ctf2 = GPIO_DT_SPEC_GET(DT_ALIAS(ctf2), gpios);
static const struct gpio_dt_spec ctf3 = GPIO_DT_SPEC_GET(DT_ALIAS(ctf3), gpios);
// GPIO for LoRa
static const struct gpio_dt_spec sx1262_cs_pin = GPIO_DT_SPEC_GET(DT_ALIAS(sx1262_cs_pin), gpios);
static const struct gpio_dt_spec sx1262_reset_pin = GPIO_DT_SPEC_GET(DT_ALIAS(sx1262_reset_pin), gpios);
static const struct gpio_dt_spec sx1262_busy_pin = GPIO_DT_SPEC_GET(DT_ALIAS(sx1262_busy_pin), gpios);
static const struct gpio_dt_spec sx1262_dio1_pin = GPIO_DT_SPEC_GET(DT_ALIAS(sx1262_dio1_pin), gpios);

// LED array for cycling animation 
const struct gpio_dt_spec *LEDs[3] = {&led2, &led0, &led1};

// USB context
static struct usbd_context *catsniffer_usbd;

// Thread definitions
#define LORA_THREAD_STACK_SIZE 2048
K_THREAD_STACK_DEFINE(lora_thread_stack, LORA_THREAD_STACK_SIZE);
static struct k_thread lora_thread;

// SX1262 instance and status
static sx1262_t sx1262_radio = {0};
static bool sx1262_initialized = false;

// SX1262 receive buffer
static uint8_t rx_buffer[255];
static uint8_t rx_length = 0;


// RACE CONDITION FIX: Safe ring buffer operations
static inline uint32_t safe_ring_buf_put(struct ring_buf *rb, const uint8_t *data, uint32_t size)
{
    unsigned int key = irq_lock();
    uint32_t result = ring_buf_put(rb, data, size);
    irq_unlock(key);
    return result;
}

static inline uint32_t safe_ring_buf_get(struct ring_buf *rb, uint8_t *data, uint32_t size)
{
    unsigned int key = irq_lock();
    uint32_t result = ring_buf_get(rb, data, size);
    irq_unlock(key);
    return result;
}

// USB message callback
static void catsniffer_usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
    if (usbd_can_detect_vbus(ctx)) {
        if (msg->type == USBD_MSG_VBUS_READY) {
            usbd_enable(ctx);
        } else if (msg->type == USBD_MSG_VBUS_REMOVED) {
            usbd_disable(ctx);
        }
    }
}

static int enable_usb_device_next(void)
{
    catsniffer_usbd = usbd_init_device(catsniffer_usb_msg_cb);
    if (catsniffer_usbd == NULL) {
        return -ENODEV;
    }
    if (!usbd_can_detect_vbus(catsniffer_usbd)) {
        return usbd_enable(catsniffer_usbd);
    }
    return 0;
}

// CC1352 UART interrupt handler - RACE CONDITION FIXED
static void cc1352_uart_interrupt_handler(const struct device *dev, void *user_data)
{
    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint8_t buf[64];
            int len = uart_fifo_read(dev, buf, sizeof(buf));
            if (len > 0) {
                
                safe_ring_buf_put(&rb_cc1352_to_usb, buf, len);
                uart_irq_tx_enable(cdc0_dev);
            }
        }

        if (uart_irq_tx_ready(dev)) {
            uint8_t buf[64];
            
            int len = safe_ring_buf_get(&rb_usb_to_cc1352, buf, sizeof(buf));
            if (len > 0) {
                uart_fifo_fill(dev, buf, len);
            } else {
                uart_irq_tx_disable(dev);
            }
        }
    }
}

// CDC0 (CC1352) interrupt handler - RACE CONDITION FIXED
static void cdc0_interrupt_handler(const struct device *dev, void *user_data)
{
    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint8_t buf[64];
            int len = uart_fifo_read(dev, buf, sizeof(buf));
            
            for (int i = 0; i < len; i++) {
                uint8_t data = buf[i];
                
                if (data == commandID[catsniffer.command_counter]) {
                    catsniffer.command_counter++;
                    if (catsniffer.command_counter == 4) {
                        catsniffer.command_recognized = true;
                        continue;
                    }
                } else if (!catsniffer.command_recognized) {
                    catsniffer.command_counter = 0;
                }
                
                if (catsniffer.command_recognized) {
                    if (catsniffer.command_data_len < COMMAND_BUF_SIZE - 1) {
                        catsniffer.command_data[catsniffer.command_data_len++] = data;
                    }
                    
                if (catsniffer.command_data_len >= 3 &&
                    catsniffer.command_data[catsniffer.command_data_len-3] == 0xC3 &&
                    catsniffer.command_data[catsniffer.command_data_len-2] == 0xBF &&  
                    catsniffer.command_data[catsniffer.command_data_len-1] == 0xC3) {  
                        
                    process_command(catsniffer.command_data, catsniffer.command_data_len);
                    catsniffer.command_recognized = false;
                    catsniffer.command_data_len = 0;
                    catsniffer.command_counter = 0;
                    }
                } else {
                    safe_ring_buf_put(&rb_usb_to_cc1352, &data, 1);
                    uart_irq_tx_enable(uart_cc1352);
                }
            }
        }
        
        if (uart_irq_tx_ready(dev)) {
            uint8_t buf[64];
            int len = safe_ring_buf_get(&rb_cc1352_to_usb, buf, sizeof(buf));
            if (len > 0) {
                uart_fifo_fill(dev, buf, len);
            } else {
                uart_irq_tx_disable(dev);
            }
        }
    }
}

// CDC1 (SX1262) interrupt handler - RACE CONDITION FIXED
static void cdc1_interrupt_handler(const struct device *dev, void *user_data)
{
    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint8_t buf[64];
            int len = uart_fifo_read(dev, buf, sizeof(buf));
            if (len > 0) {
                
                safe_ring_buf_put(&rb_usb_to_sx1262, buf, len);
            }
        }
        
        if (uart_irq_tx_ready(dev)) {
            uint8_t buf[64];
            
            int len = safe_ring_buf_get(&rb_sx1262_to_usb, buf, sizeof(buf));
            if (len > 0) {
                uart_fifo_fill(dev, buf, len);
            } else {
                uart_irq_tx_disable(dev);
            }
        }
    }
}

void reset_cc1352(void)
{
    gpio_pin_set_dt(&pin_reset, 0);
    k_msleep(100);
    gpio_pin_set_dt(&pin_reset, 1);
    k_msleep(100);
}

void boot_mode_cc1352(void)
{
    gpio_pin_configure_dt(&pin_boot, GPIO_OUTPUT);
    gpio_pin_set_dt(&pin_boot, 0);
    k_msleep(100);
    reset_cc1352();
}

void change_baud(unsigned long new_baud)
{
    if (new_baud == catsniffer.baud) return;
    
    uart_irq_tx_disable(uart_cc1352);
    uart_irq_rx_disable(uart_cc1352);
    
    struct uart_config cfg;
    uart_config_get(uart_cc1352, &cfg);
    cfg.baudrate = new_baud;
    uart_configure(uart_cc1352, &cfg);
    catsniffer.baud = new_baud;
    
    // Re-enable interrupts
    uart_irq_rx_enable(uart_cc1352);
}

void change_band(unsigned long new_band)
{
    if (new_band == catsniffer.band) return;
    
    switch (new_band) {
        case GIG:
            gpio_pin_set_dt(&ctf1, 0);
            gpio_pin_set_dt(&ctf2, 1);
            gpio_pin_set_dt(&ctf3, 0);
            break;
        case SUBGIG_1:
            gpio_pin_set_dt(&ctf1, 0);
            gpio_pin_set_dt(&ctf2, 0);
            gpio_pin_set_dt(&ctf3, 1);
            break;
        case SUBGIG_2:
            gpio_pin_set_dt(&ctf1, 1);
            gpio_pin_set_dt(&ctf2, 0);
            gpio_pin_set_dt(&ctf3, 0);
            break;
    }
    catsniffer.band = new_band;
}

void change_mode(unsigned long new_mode)
{
    if (new_mode == catsniffer.mode) return;
    
    catsniffer.mode = new_mode;
    
    switch (new_mode) {
        case BOOT:
            catsniffer.led_interval = 200;
            boot_mode_cc1352();
            reset_cc1352();
            k_msleep(200);
            change_baud(500000);
            break;
        case PASSTHROUGH:
            gpio_pin_configure_dt(&pin_boot, GPIO_INPUT | GPIO_PULL_UP);
            reset_cc1352();
            catsniffer.led_interval = 1000;
            change_baud(921600);
            break;
    }
}

// SX1262 TX done callback
void sx1262_tx_done_callback(void)
{
    const char *msg = "TX_DONE\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)msg, strlen(msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
}

// SX1262 RX done callback
void sx1262_rx_done_callback(uint8_t *data, uint8_t len, int16_t rssi, int8_t snr)
{
    char response[512];
    char hex_data[256] = {0};
    
    // Convert received data to hex string
    for (uint8_t i = 0; i < len && i < 127; i++) {
        snprintf(&hex_data[i*2], 3, "%02X", data[i]);
    }
    
    snprintf(response, sizeof(response), 
             "RX_DONE: %u bytes, RSSI=%d dBm, SNR=%d dB, Data=%s\n", 
             len, rssi, snr, hex_data);
    
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)response, strlen(response));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
}

// SX1262 RX error callback
void sx1262_rx_error_callback(void)
{
    const char *msg = "RX_ERROR\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)msg, strlen(msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
}

// SX1262 interrupt work handler
static void sx1262_irq_work_handler(struct k_work *work)
{
    uint16_t irq_status;
    int ret = sx1262_get_irq_status(&sx1262_radio, &irq_status);
    
    if (ret < 0) {
        return;
    }
    
    // Clear all interrupts
    sx1262_clear_irq_status(&sx1262_radio, irq_status);
    
    // Handle TX done
    if (irq_status & SX1262_IRQ_TX_DONE) {
        if (sx1262_radio.tx_done_callback) {
            sx1262_radio.tx_done_callback();
        }
    }
    
    // Handle RX done
    if (irq_status & SX1262_IRQ_RX_DONE) {
        // Get packet status for RSSI and SNR
        uint8_t packet_status[3];
        sx1262_read_command(&sx1262_radio, SX1262_CMD_GET_PACKET_STATUS, packet_status, 3);
        
        int16_t rssi = -packet_status[0] / 2;
        int8_t snr = packet_status[1] / 4;
        
        // Get received data
        uint8_t rx_status[2];
        sx1262_read_command(&sx1262_radio, SX1262_CMD_GET_RX_BUFFER_STATUS, rx_status, 2);
        
        uint8_t payload_length = rx_status[0];
        uint8_t rx_start_buffer = rx_status[1];
        
        if (payload_length > 0 && payload_length <= sizeof(rx_buffer)) {
            sx1262_read_buffer(&sx1262_radio, rx_start_buffer, rx_buffer, payload_length);
            rx_length = payload_length;
            
            if (sx1262_radio.rx_done_callback) {
                sx1262_radio.rx_done_callback(rx_buffer, payload_length, rssi, snr);
            }
        }
    }
    
    // Handle RX timeout
    if (irq_status & SX1262_IRQ_TIMEOUT) {
        const char *msg = "RX_TIMEOUT\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)msg, strlen(msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    }
    
    // Handle CRC error
    if (irq_status & SX1262_IRQ_CRC_ERR) {
        if (sx1262_radio.rx_error_callback) {
            sx1262_radio.rx_error_callback();
        }
    }
}

// SX1262 DIO1 interrupt handler
static void sx1262_dio1_interrupt(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // Schedule work to handle interrupt in thread context
    k_work_submit(&sx1262_radio.irq_work);
}

int setup_sx1262_interrupts(void)
{    
    // Initialize interrupt work
    k_work_init(&sx1262_radio.irq_work, sx1262_irq_work_handler);
    
    // Setup callbacks
    sx1262_radio.tx_done_callback = sx1262_tx_done_callback;
    sx1262_radio.rx_done_callback = sx1262_rx_done_callback;
    sx1262_radio.rx_error_callback = sx1262_rx_error_callback;
    
    const char *status_msg = "SX1262: Setting up interrupts...\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    // Configure DIO1 interrupt (use the static GPIO pins directly)
    gpio_init_callback(&sx1262_radio.dio1_callback, sx1262_dio1_interrupt, 
                       BIT(sx1262_dio1_pin.pin));
    
    int ret = gpio_add_callback(sx1262_dio1_pin.port, &sx1262_radio.dio1_callback);
    if (ret < 0) {
        status_msg = "ERROR: Failed to add GPIO callback (%d)\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return ret;
    }
    
    ret = gpio_pin_interrupt_configure_dt(&sx1262_dio1_pin, GPIO_INT_EDGE_RISING);
    if (ret < 0) {
        status_msg = "ERROR: Failed to configure GPIO interrupt (%d)\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return ret;
    }
    
    status_msg = "SX1262: Interrupts configured successfully!\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    return 0;
}

int initialize_sx1262(void)
{
    const char *status_msg = "Initialize SX1276!\n";
    safe_ring_buf_put(&rb_cc1352_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    uart_irq_tx_enable(cdc1_dev);

    if (sx1262_initialized) {
        status_msg = "SX1262: Already initialized\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return 0;
    }

    
    // Get SPI device
    sx1262_radio.spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
    if (!device_is_ready(sx1262_radio.spi_dev)) {
        status_msg = "ERROR: SPI device not ready\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return -ENODEV;
    }
    
    status_msg = "SX1262: SPI device ready\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    // Configure SPI
    sx1262_radio.spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
    sx1262_radio.spi_cfg.frequency = 8000000; // 8MHz
    sx1262_radio.spi_cfg.slave = 0;
    
    // Check GPIO devices (don't assign to const struct members)
    if (!device_is_ready(sx1262_cs_pin.port) || 
        !device_is_ready(sx1262_reset_pin.port) || 
        !device_is_ready(sx1262_busy_pin.port) ||
        !device_is_ready(sx1262_dio1_pin.port)) {
        status_msg = "ERROR: SX1262 GPIO devices not ready\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return -ENODEV;
    }
    
    status_msg = "SX1262: GPIO devices ready\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    status_msg = "SX1262: GPIO pins configured\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    // Set default LoRa parameters
    sx1262_radio.frequency = 868000000; // 868 MHz
    sx1262_radio.tx_power = 14;         // 14 dBm
    sx1262_radio.spreading_factor = 7;   // SF7
    sx1262_radio.bandwidth = 0x04;       // 125 kHz
    sx1262_radio.coding_rate = 0x01;     // 4/5
    sx1262_radio.crc_enabled = true;
    
    // Reset the radio (use the static GPIO pins directly)
    status_msg = "SX1262: Resetting radio...\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    gpio_pin_set_dt(&sx1262_reset_pin, 0);
    k_msleep(1);
    gpio_pin_set_dt(&sx1262_reset_pin, 1);
    k_msleep(100);
    
    // Wait for BUSY to go low
    int timeout = 1000;
    while (gpio_pin_get_dt(&sx1262_busy_pin) && timeout > 0) {
        k_usleep(100);
        timeout--;
    }
    
    if (timeout <= 0) {
        status_msg = "ERROR: SX1262 BUSY timeout\n";
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return -ETIMEDOUT;
    }
    
    status_msg = "SX1262: Reset complete\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    sx1262_initialized = true;
    status_msg = "SX1262: Initialization completed successfully!\n";
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)status_msg, strlen(status_msg));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
    
    return 0;
}

void process_command(char *cmd, size_t len)
{
    char debug_msg[128];
    
    // The buffer contains: ['b', 'o', 'o', 't', '>', 0xFF, 0xF1]
    // We need to extract just the payload before the '>'
    
    char *payload_end = NULL;
    for (size_t i = 0; i < len; i++) {
        if (cmd[i] == '>') {
            payload_end = &cmd[i];
            break;
        }
    }
    
    if (!payload_end) {
        snprintf(debug_msg, sizeof(debug_msg), "ERROR: No > found\n");
        safe_ring_buf_put(&rb_cc1352_to_usb, (uint8_t*)debug_msg, strlen(debug_msg));
        uart_irq_tx_enable(cdc0_dev);
        return;
    }
    
    size_t payload_len = payload_end - cmd;
    
    char payload[64];
    if (payload_len >= sizeof(payload)) {
        snprintf(debug_msg, sizeof(debug_msg), "ERROR: Payload too long\n");
        safe_ring_buf_put(&rb_cc1352_to_usb, (uint8_t*)debug_msg, strlen(debug_msg));
        uart_irq_tx_enable(cdc0_dev);
        return;
    }
    
    memcpy(payload, cmd, payload_len);
    payload[payload_len] = '\0';
    
    const char *response;
    
    if (strcmp(payload, "boot") == 0) {
        change_mode(BOOT);
        response = "BOOT\n";
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, catsniffer.mode);
    }
    else if (strcmp(payload, "exit") == 0) {
        change_mode(PASSTHROUGH);
        response = "PASSTHROUGH\n";
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, 0);
    }
    else if (strcmp(payload, "band1") == 0) {
        change_band(GIG);
        response = "2.4GHz Band\n";
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, 0);
    }
    else if (strcmp(payload, "band2") == 0) {
        change_band(SUBGIG_1);
        response = "SUB-GHz Band\n";
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, 0);
    }
    else if (strcmp(payload, "band3") == 0) {
        change_band(SUBGIG_2);
        response = "LoRa Band\n";
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, 0);
    }
    else {
        response = "UNKNOWN\n";
    }
    
    safe_ring_buf_put(&rb_cc1352_to_usb, (uint8_t*)response, strlen(response));
    uart_irq_tx_enable(cdc0_dev);
}

void process_lora_command(char *cmd_line)
{
    char response[256];
    
    // TEST command - simple SX1262 test
    if (strncmp(cmd_line, "TEST", 4) == 0) {
        snprintf(response, sizeof(response), "TEST: Starting SX1262 test...\n");
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)response, strlen(response));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        
        // Force re-initialization for testing
        sx1262_initialized = false;
        int ret = initialize_sx1262();
        
        if (ret == 0) {
            ret = setup_sx1262_interrupts();
            if (ret == 0) {
                snprintf(response, sizeof(response), "TEST: SX1262 fully ready!\n");
            } else {
                snprintf(response, sizeof(response), "TEST: Init OK, but interrupt setup failed (%d)\n", ret);
            }
        } else {
            snprintf(response, sizeof(response), "TEST: Initialization failed (%d)\n", ret);
        }
        
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)response, strlen(response));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return;
    }
    
    // STATUS command
    if (strncmp(cmd_line, "STATUS", 6) == 0) {
        if (sx1262_initialized) {
            snprintf(response, sizeof(response), "STATUS: SX1262 initialized and ready\n");
        } else {
            snprintf(response, sizeof(response), "STATUS: SX1262 not initialized\n");
        }
        safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)response, strlen(response));
        if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
        return;
    }
    
    // Default response
    snprintf(response, sizeof(response), "Available commands: TEST, STATUS\n");
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)response, strlen(response));
    if (cdc1_dev) uart_irq_tx_enable(cdc1_dev);
}

// LoRa thread function 
static void lora_thread_func(void *p1, void *p2, void *p3)
{
    char command_buffer[128];
    size_t cmd_len = 0;
    
    while (1) {
        uint8_t usb_buf[64];  
        int usb_len = safe_ring_buf_get(&rb_usb_to_sx1262, usb_buf, sizeof(usb_buf));
        
        if (usb_len > 0) {
            for (int i = 0; i < usb_len; i++) {
                char c = usb_buf[i];
                
                if (c == '\n' || c == '\r') {
                    if (cmd_len > 0) {
                        command_buffer[cmd_len] = '\0';
                        process_lora_command(command_buffer);
                        cmd_len = 0;
                    }
                } else if (cmd_len < sizeof(command_buffer) - 1) {
                    command_buffer[cmd_len++] = c;
                }
            }
        }
        k_msleep(10);
    }
}

int main(void)
{
    int ret;
    // Get device references
    uart_cc1352 = CC1352_UART;
    cdc0_dev = CDC0_DEV;
    cdc1_dev = CDC1_DEV; 
    
    // Initialize GPIO
    INIT_GPIO(pin_reset, GPIO_OUTPUT);
    INIT_GPIO(pin_boot, GPIO_INPUT | GPIO_PULL_UP);
    INIT_GPIO(led0, GPIO_OUTPUT);
    INIT_GPIO(led1, GPIO_OUTPUT);
    INIT_GPIO(led2, GPIO_OUTPUT);
    INIT_GPIO(ctf1, GPIO_OUTPUT);
    INIT_GPIO(ctf2, GPIO_OUTPUT);
    INIT_GPIO(ctf3, GPIO_OUTPUT);

    INIT_GPIO(cjtag0, GPIO_INPUT);
    INIT_GPIO(cjtag1, GPIO_INPUT);
    INIT_GPIO(cjtag2, GPIO_INPUT);
    INIT_GPIO(cjtag3, GPIO_INPUT);

    INIT_GPIO(sx1262_cs_pin, GPIO_OUTPUT_INACTIVE);
    INIT_GPIO(sx1262_reset_pin, GPIO_OUTPUT_ACTIVE); 
    INIT_GPIO(sx1262_busy_pin, GPIO_INPUT);
    INIT_GPIO(sx1262_dio1_pin, GPIO_INPUT);

    gpio_pin_set_dt(&pin_reset, 1);
    
    // Check device readiness
    if (!device_is_ready(cdc0_dev) || !device_is_ready(uart_cc1352)) {
        return -ENODEV;
    }
    
    // Determine mode 
    if (!gpio_pin_get_dt(&pin_boot)) {
        catsniffer.led_interval = 200;
        catsniffer.baud = 500000;
        catsniffer.mode = BOOT;
    } else {
        catsniffer.led_interval = 1000;
        catsniffer.baud = 921600;
        catsniffer.mode = PASSTHROUGH;
    }
    
    // Timeout for boot pin - don't hang forever
    int timeout = 0;
    while (!gpio_pin_get_dt(&pin_boot) && timeout < 50) {
        k_msleep(10);
        timeout++;
    }

    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);

    // Initialize USB
    ret = enable_usb_device_next();
    if (ret < 0) {
        return ret;
    }
    gpio_pin_set_dt(&led0, 1);

    // Initialize ALL ring buffers
    ring_buf_init(&rb_cc1352_to_usb, sizeof(ring_cc1352_to_usb), ring_cc1352_to_usb);
    ring_buf_init(&rb_usb_to_cc1352, sizeof(ring_usb_to_cc1352), ring_usb_to_cc1352);
    ring_buf_init(&rb_sx1262_to_usb, sizeof(ring_sx1262_to_usb), ring_sx1262_to_usb);
    ring_buf_init(&rb_usb_to_sx1262, sizeof(ring_usb_to_sx1262), ring_usb_to_sx1262);
    
    // Configure UART
    struct uart_config uart_cfg;
    uart_config_get(uart_cc1352, &uart_cfg);
    uart_cfg.baudrate = catsniffer.baud;
    uart_configure(uart_cc1352, &uart_cfg);
    
    // Set up interrupt handlers
    uart_irq_callback_set(cdc0_dev, cdc0_interrupt_handler);
    uart_irq_rx_enable(cdc0_dev);
    
    // Only set up cdc1 if it exists
    if (cdc1_dev) {
        uart_irq_callback_set(cdc1_dev, cdc1_interrupt_handler);
        uart_irq_rx_enable(cdc1_dev);
    }
    
    uart_irq_callback_set(uart_cc1352, cc1352_uart_interrupt_handler);
    uart_irq_rx_enable(uart_cc1352);
    
    reset_cc1352();
    
    if (catsniffer.mode == BOOT) {
        boot_mode_cc1352();
    } else {
        change_band(GIG);
    }
    
    // Set initial LED states
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);
    
    // Send startup message AFTER everything is initialized
    k_msleep(1000); // Wait a moment for USB to be ready
    const char *startup_msg = "Catsniffer Firmware Ready - Send commands!\n";
    safe_ring_buf_put(&rb_cc1352_to_usb, (uint8_t*)startup_msg, strlen(startup_msg));
    uart_irq_tx_enable(cdc0_dev);
    safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t*)startup_msg, strlen(startup_msg));
    uart_irq_tx_enable(cdc1_dev);

        
    ret = initialize_sx1262();
    if (ret < 0) {
        LOG_ERR("SX1262 initialization failed: %d", ret);
        // Continue anyway, LoRa commands will show error when used
    } else {
        ret = setup_sx1262_interrupts();
        if (ret < 0) {
            LOG_ERR("SX1262 interrupt setup failed: %d", ret);
        } else {
            LOG_INF("SX1262 initialized successfully");
        }
    }
    
    // Start LoRa thread
    k_thread_create(&lora_thread, lora_thread_stack, LORA_THREAD_STACK_SIZE,
                    lora_thread_func, NULL, NULL, NULL,
                    K_PRIO_COOP(5), 0, K_NO_WAIT);
    
    // Main loop with LED animation 
    while (1) {
        int64_t current_time = k_uptime_get();
        if (current_time - catsniffer.previous_millis > catsniffer.led_interval) {
            catsniffer.previous_millis = current_time;
            //Check catsniffer mode
            if (catsniffer.mode) { 
                static int led_index = 0;
                // Cycle through LEDs
                gpio_pin_toggle_dt(LEDs[led_index]);
                led_index++;
                if (led_index > 2) led_index = 0;
            } else {
                // Just toggle LED2
                gpio_pin_toggle_dt(&led2);
            }
        }
        
        k_msleep(10);
    }
    
    return 0;
}