/*
 * SX1262 LoRa Driver Header for Catsniffer
 * Eduardo Contreras @ Electronic Cats
 * 
 * This header defines the SX1262 LoRa transceiver interface
 */

#ifndef SX1262_DRIVER_H
#define SX1262_DRIVER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

// SX1262 Commands
#define SX1262_CMD_GET_STATUS           0xC0
#define SX1262_CMD_WRITE_REGISTER       0x0D
#define SX1262_CMD_READ_REGISTER        0x1D
#define SX1262_CMD_WRITE_BUFFER         0x0E
#define SX1262_CMD_READ_BUFFER          0x1E
#define SX1262_CMD_SET_SLEEP            0x84
#define SX1262_CMD_SET_STANDBY          0x80
#define SX1262_CMD_SET_FS               0xC1
#define SX1262_CMD_SET_TX               0x83
#define SX1262_CMD_SET_RX               0x82
#define SX1262_CMD_SET_RF_FREQUENCY     0x86
#define SX1262_CMD_SET_PA_CONFIG        0x95
#define SX1262_CMD_SET_TX_PARAMS        0x8E
#define SX1262_CMD_SET_MODULATION_PARAMS 0x8B
#define SX1262_CMD_SET_PACKET_PARAMS    0x8C
#define SX1262_CMD_GET_RX_BUFFER_STATUS 0x13
#define SX1262_CMD_GET_PACKET_STATUS    0x14
#define SX1262_CMD_GET_RSSI_INST        0x15
#define SX1262_CMD_GET_STATS            0x10
#define SX1262_CMD_RESET_STATS          0x00
#define SX1262_CMD_CFG_DIO_IRQ          0x08
#define SX1262_CMD_GET_IRQ_STATUS       0x12
#define SX1262_CMD_CLR_IRQ_STATUS       0x02
#define SX1262_CMD_CALIBRATE            0x89
#define SX1262_CMD_CALIBRATE_IMAGE      0x98
#define SX1262_CMD_SET_REGULATOR_MODE   0x96

// SX1262 Status
#define SX1262_STATUS_MODE_MASK         0x70
#define SX1262_STATUS_CMD_MASK          0x0E

// SX1262 Modes
#define SX1262_MODE_SLEEP               0x00
#define SX1262_MODE_STBY_RC             0x20
#define SX1262_MODE_STBY_XOSC           0x30
#define SX1262_MODE_FS                  0x40
#define SX1262_MODE_TX                  0x60
#define SX1262_MODE_RX                  0x50

// IRQ definitions
#define SX1262_IRQ_TX_DONE              (1 << 0)
#define SX1262_IRQ_RX_DONE              (1 << 1)
#define SX1262_IRQ_PREAMBLE_DETECTED    (1 << 2)
#define SX1262_IRQ_SYNC_WORD_VALID      (1 << 3)
#define SX1262_IRQ_HEADER_VALID         (1 << 4)
#define SX1262_IRQ_HEADER_ERR           (1 << 5)
#define SX1262_IRQ_CRC_ERR              (1 << 6)
#define SX1262_IRQ_CAD_DONE             (1 << 7)
#define SX1262_IRQ_CAD_DETECTED         (1 << 8)
#define SX1262_IRQ_TIMEOUT              (1 << 9)

// SX1262 structure
typedef struct {
    const struct device *spi_dev;
    struct spi_config spi_cfg;
    const struct gpio_dt_spec cs_pin;
    const struct gpio_dt_spec reset_pin;
    const struct gpio_dt_spec busy_pin;
    const struct gpio_dt_spec dio1_pin;
    
    uint32_t frequency;
    int8_t tx_power;
    uint8_t spreading_factor;
    uint8_t bandwidth;
    uint8_t coding_rate;
    bool crc_enabled;
    
    k_tid_t irq_thread_id;
    struct k_work irq_work;
    struct gpio_callback dio1_callback;
    
    // Callbacks
    void (*tx_done_callback)(void);
    void (*rx_done_callback)(uint8_t *data, uint8_t len, int16_t rssi, int8_t snr);
    void (*rx_error_callback)(void);
} sx1262_t;

// Function prototypes
int sx1262_init(sx1262_t *sx);
int sx1262_reset(sx1262_t *sx);
int sx1262_write_command(sx1262_t *sx, uint8_t cmd, uint8_t *data, uint8_t len);
int sx1262_read_command(sx1262_t *sx, uint8_t cmd, uint8_t *data, uint8_t len);
int sx1262_write_register(sx1262_t *sx, uint16_t addr, uint8_t value);
uint8_t sx1262_read_register(sx1262_t *sx, uint16_t addr);
int sx1262_write_buffer(sx1262_t *sx, uint8_t offset, uint8_t *data, uint8_t len);
int sx1262_read_buffer(sx1262_t *sx, uint8_t offset, uint8_t *data, uint8_t len);
uint8_t sx1262_get_status(sx1262_t *sx);
int sx1262_set_standby(sx1262_t *sx, uint8_t mode);
int sx1262_set_packet_type(sx1262_t *sx, uint8_t packet_type);
int sx1262_set_rf_frequency(sx1262_t *sx, uint32_t frequency);
int sx1262_set_tx_params(sx1262_t *sx, int8_t power, uint8_t ramp_time);
int sx1262_set_modulation_params(sx1262_t *sx);
int sx1262_set_packet_params(sx1262_t *sx, uint16_t preamble_len, uint8_t header_type, 
                           uint8_t payload_len, uint8_t crc_type, uint8_t invert_iq);
int sx1262_send(sx1262_t *sx, uint8_t *data, uint8_t len);
int sx1262_receive(sx1262_t *sx, uint32_t timeout);
int sx1262_set_dio_irq_params(sx1262_t *sx, uint16_t irq_mask, uint16_t dio1_mask, 
                             uint16_t dio2_mask, uint16_t dio3_mask);
int sx1262_get_irq_status(sx1262_t *sx, uint16_t *irq_status);
int sx1262_clear_irq_status(sx1262_t *sx, uint16_t irq_mask);
void sx1262_wait_on_busy(sx1262_t *sx);

#endif /* SX1262_DRIVER_H */