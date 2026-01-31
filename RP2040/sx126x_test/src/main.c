/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal test for native SX126x driver on CatSniffer (RP2040)
 *
 * LED feedback:
 *   LED0 (GPIO27): Fast blink = LoRa init FAILED
 *   LED1 (GPIO26): Slow blink = LoRa working, heartbeat
 *   LED2 (GPIO28): Toggle on each successful TX
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/lora.h>

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
	     "No default LoRa radio specified in DT");

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static void blink_error(void)
{
	/* Fast blink LED0 forever to indicate error */
	while (1) {
		gpio_pin_toggle_dt(&led0);
		k_msleep(100);
	}
}

int main(void)
{
	const struct device *lora_dev;
	struct lora_modem_config config;
	int ret;
	int tx_count = 0;

	/* Initialize LEDs */
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);

	/* Get LoRa device */
	lora_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
	if (!device_is_ready(lora_dev)) {
		blink_error();
	}

	/* Configure for LoRa TX */
	config.frequency = 915000000;
	config.bandwidth = BW_125_KHZ;
	config.datarate = SF_10;
	config.preamble_len = 8;
	config.coding_rate = CR_4_5;
	config.tx_power = 14;
	config.tx = true;

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		blink_error();
	}

	/* LoRa initialized successfully - now send packets */
	char data[32];

	while (1) {
		/* Heartbeat on LED1 */
		gpio_pin_toggle_dt(&led1);

		/* Send test packet */
		snprintf(data, sizeof(data), "CatSniffer TX #%d", tx_count++);
		ret = lora_send(lora_dev, data, strlen(data));

		if (ret == 0) {
			/* Success - toggle LED2 */
			gpio_pin_toggle_dt(&led2);
		}

		k_msleep(2000);
	}

	return 0;
}
