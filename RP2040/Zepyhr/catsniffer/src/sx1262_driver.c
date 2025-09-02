/*
 * SX1262 LoRa Driver Implementation for Catsniffer
 * Eduardo Contreras @ Electronic Cats
 */

#include "sx1262_driver.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(sx1262_driver, LOG_LEVEL_INF);

void sx1262_wait_on_busy(sx1262_t *sx)
{
    while (gpio_pin_get_dt(&sx->busy_pin)) {
        k_usleep(10);
    }
}

int sx1262_reset(sx1262_t *sx)
{
    gpio_pin_set_dt(&sx->reset_pin, 0);
    k_msleep(1);
    gpio_pin_set_dt(&sx->reset_pin, 1);
    k_msleep(100);
    sx1262_wait_on_busy(sx);
    return 0;
}

int sx1262_write_command(sx1262_t *sx, uint8_t cmd, uint8_t *data, uint8_t len)
{
    sx1262_wait_on_busy(sx);
    
    uint8_t tx_buf[256];
    tx_buf[0] = cmd;
    if (data && len > 0) {
        memcpy(&tx_buf[1], data, len);
    }
    
    struct spi_buf tx_spi_buf = {
        .buf = tx_buf,
        .len = len + 1
    };
    
    struct spi_buf_set tx_spi_buf_set = {
        .buffers = &tx_spi_buf,
        .count = 1
    };
    
    return spi_transceive(sx->spi_dev, &sx->spi_cfg, &tx_spi_buf_set, NULL);
}

int sx1262_read_command(sx1262_t *sx, uint8_t cmd, uint8_t *data, uint8_t len)
{
    sx1262_wait_on_busy(sx);
    
    uint8_t tx_buf[256] = {cmd};
    uint8_t rx_buf[256];
    
    struct spi_buf tx_spi_buf = {
        .buf = tx_buf,
        .len = len + 2
    };
    
    struct spi_buf rx_spi_buf = {
        .buf = rx_buf,
        .len = len + 2
    };
    
    struct spi_buf_set tx_spi_buf_set = {
        .buffers = &tx_spi_buf,
        .count = 1
    };
    
    struct spi_buf_set rx_spi_buf_set = {
        .buffers = &rx_spi_buf,
        .count = 1
    };
    
    int ret = spi_transceive(sx->spi_dev, &sx->spi_cfg, &tx_spi_buf_set, &rx_spi_buf_set);
    if (ret == 0 && data && len > 0) {
        memcpy(data, &rx_buf[2], len);
    }
    
    return ret;
}

uint8_t sx1262_get_status(sx1262_t *sx)
{
    uint8_t status;
    sx1262_read_command(sx, SX1262_CMD_GET_STATUS, &status, 1);
    return status;
}

int sx1262_set_standby(sx1262_t *sx, uint8_t mode)
{
    uint8_t data = mode;
    return sx1262_write_command(sx, SX1262_CMD_SET_STANDBY, &data, 1);
}

int sx1262_set_packet_type(sx1262_t *sx, uint8_t packet_type)
{
    uint8_t data = packet_type;
    return sx1262_write_command(sx, SX1262_CMD_SET_PACKET_PARAMS, &data, 1);
}

int sx1262_set_rf_frequency(sx1262_t *sx, uint32_t frequency)
{
    uint8_t data[4];
    uint32_t freq_raw = (uint32_t)((double)frequency / 32000000.0 * 16777216.0);
    
    data[0] = (freq_raw >> 24) & 0xFF;
    data[1] = (freq_raw >> 16) & 0xFF;
    data[2] = (freq_raw >> 8) & 0xFF;
    data[3] = freq_raw & 0xFF;
    
    sx->frequency = frequency;
    return sx1262_write_command(sx, SX1262_CMD_SET_RF_FREQUENCY, data, 4);
}

int sx1262_set_tx_params(sx1262_t *sx, int8_t power, uint8_t ramp_time)
{
    uint8_t data[2];
    data[0] = power;
    data[1] = ramp_time;
    
    sx->tx_power = power;
    return sx1262_write_command(sx, SX1262_CMD_SET_TX_PARAMS, data, 2);
}

int sx1262_set_modulation_params(sx1262_t *sx)
{
    uint8_t data[4];
    data[0] = sx->spreading_factor;
    data[1] = sx->bandwidth;
    data[2] = sx->coding_rate;
    data[3] = 0;
    
    return sx1262_write_command(sx, SX1262_CMD_SET_MODULATION_PARAMS, data, 4);
}

int sx1262_send(sx1262_t *sx, uint8_t *data, uint8_t len)
{
    int ret;
    
    ret = sx1262_write_buffer(sx, 0, data, len);
    if (ret < 0) return ret;
    
    uint8_t tx_data[3] = {0x00, 0x00, 0x00};
    return sx1262_write_command(sx, SX1262_CMD_SET_TX, tx_data, 3);
}

int sx1262_receive(sx1262_t *sx, uint32_t timeout)
{
    uint8_t data[3];
    data[0] = (timeout >> 16) & 0xFF;
    data[1] = (timeout >> 8) & 0xFF;
    data[2] = timeout & 0xFF;
    
    return sx1262_write_command(sx, SX1262_CMD_SET_RX, data, 3);
}

int sx1262_write_buffer(sx1262_t *sx, uint8_t offset, uint8_t *data, uint8_t len)
{
    sx1262_wait_on_busy(sx);
    
    uint8_t tx_buf[256];
    tx_buf[0] = SX1262_CMD_WRITE_BUFFER;
    tx_buf[1] = offset;
    memcpy(&tx_buf[2], data, len);
    
    struct spi_buf tx_spi_buf = {
        .buf = tx_buf,
        .len = len + 2
    };
    
    struct spi_buf_set tx_spi_buf_set = {
        .buffers = &tx_spi_buf,
        .count = 1
    };
    
    return spi_write(sx->spi_dev, &sx->spi_cfg, &tx_spi_buf_set);
}

int sx1262_read_buffer(sx1262_t *sx, uint8_t offset, uint8_t *data, uint8_t len)
{
    sx1262_wait_on_busy(sx);
    
    uint8_t tx_buf[2] = {SX1262_CMD_READ_BUFFER, offset};
    uint8_t rx_buf[258];
    
    struct spi_buf tx_spi_buf = {
        .buf = tx_buf,
        .len = 2
    };
    
    struct spi_buf rx_spi_buf = {
        .buf = rx_buf,
        .len = len + 3
    };
    
    struct spi_buf_set tx_spi_buf_set = {
        .buffers = &tx_spi_buf,
        .count = 1
    };
    
    struct spi_buf_set rx_spi_buf_set = {
        .buffers = &rx_spi_buf,
        .count = 1
    };
    
    int ret = spi_transceive(sx->spi_dev, &sx->spi_cfg, &tx_spi_buf_set, &rx_spi_buf_set);
    if (ret == 0) {
        memcpy(data, &rx_buf[3], len);
    }
    
    return ret;
}

int sx1262_get_irq_status(sx1262_t *sx, uint16_t *irq_status)
{
    uint8_t data[2];
    int ret = sx1262_read_command(sx, SX1262_CMD_GET_IRQ_STATUS, data, 2);
    if (ret == 0) {
        *irq_status = (data[0] << 8) | data[1];
    }
    return ret;
}

int sx1262_clear_irq_status(sx1262_t *sx, uint16_t irq_mask)
{
    uint8_t data[2];
    data[0] = (irq_mask >> 8) & 0xFF;
    data[1] = irq_mask & 0xFF;
    
    return sx1262_write_command(sx, SX1262_CMD_CLR_IRQ_STATUS, data, 2);
}