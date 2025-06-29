/*
  Eduardo Contreras @ Electronic Cats
  Original Creation Date: Jun 15, 2025

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
  
*/


#include <sample_usbd.h>

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cdc_acm_passthrough, LOG_LEVEL_NONE);

#define UART_DEV    DEVICE_DT_GET(DT_CHOSEN(uart_passthrough))
#define CDC_DEV     DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart)

const struct device *const uart_dev = UART_DEV;
const struct device *const cdc_dev = CDC_DEV;

#define RING_BUF_SIZE 1024
uint8_t ring_uart_to_usb[RING_BUF_SIZE];
uint8_t ring_usb_to_uart[RING_BUF_SIZE];

struct ring_buf rb_uart_to_usb;
struct ring_buf rb_usb_to_uart;

#define SLEEP_TIME_MS 1000

#define INIT_GPIO(name, flags) \
	const struct gpio_dt_spec name = GPIO_DT_SPEC_GET_OR(DT_ALIAS(name), gpios, {0}); \
	do { \
		if (!device_is_ready(name.port)) { \
			printk("Error: " #name " device not ready\n"); \
			return 1; \
		} \
		gpio_pin_configure_dt(&name, flags); \
	} while (0)

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
static struct usbd_context *sample_usbd;
K_SEM_DEFINE(dtr_sem, 0, 1);

static void sample_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(ctx);
		} else if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(ctx);
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;
		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_sem_give(&dtr_sem);
		}
	}
}

static int enable_usb_device_next(void)
{
	sample_usbd = sample_usbd_init_device(sample_msg_cb);
	if (sample_usbd == NULL) {
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(sample_usbd)) {
		return usbd_enable(sample_usbd);
	}
	return 0;
}
#endif

static void uart_interrupt_handler(const struct device *dev, void *user_data)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64];
			int len = uart_fifo_read(dev, buf, sizeof(buf));
			if (len > 0) {
				ring_buf_put(&rb_uart_to_usb, buf, len);
				uart_irq_tx_enable(cdc_dev);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64];
			int len = ring_buf_get(&rb_usb_to_uart, buf, sizeof(buf));
			if (len > 0) {
				uart_fifo_fill(dev, buf, len);
			} else {
				uart_irq_tx_disable(dev);
			}
		}
	}
}

static void cdc_interrupt_handler(const struct device *dev, void *user_data)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64];
			int len = uart_fifo_read(dev, buf, sizeof(buf));
			if (len > 0) {
				ring_buf_put(&rb_usb_to_uart, buf, len);
				uart_irq_tx_enable(uart_dev);
			}
		}
		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64];
			int len = ring_buf_get(&rb_uart_to_usb, buf, sizeof(buf));
			if (len > 0) {
				uart_fifo_fill(dev, buf, len);
			} else {
				uart_irq_tx_disable(dev);
			}
		}
	}
}

int main(void)
{
	int ret;

	INIT_GPIO(pin_button, GPIO_INPUT | GPIO_PULL_UP);
	INIT_GPIO(pin_boot, GPIO_INPUT | GPIO_PULL_UP);
	INIT_GPIO(pin_reset_viewer, GPIO_INPUT);
	INIT_GPIO(pin_reset, GPIO_OUTPUT);
	gpio_pin_set_dt(&pin_reset, 1);

	INIT_GPIO(led0, GPIO_OUTPUT);
	INIT_GPIO(led1, GPIO_OUTPUT);
	INIT_GPIO(led2, GPIO_OUTPUT);
	gpio_pin_set_dt(&led0, 0);
	gpio_pin_set_dt(&led1, 0);
	gpio_pin_set_dt(&led2, 0);

	INIT_GPIO(ctf1, GPIO_OUTPUT);
	INIT_GPIO(ctf2, GPIO_OUTPUT);
	INIT_GPIO(ctf3, GPIO_OUTPUT);
	gpio_pin_set_dt(&ctf1, 0);
	gpio_pin_set_dt(&ctf2, 0);
	gpio_pin_set_dt(&ctf3, 0);

	INIT_GPIO(cjtag0, GPIO_INPUT);
	INIT_GPIO(cjtag1, GPIO_INPUT);
	INIT_GPIO(cjtag2, GPIO_INPUT);
	INIT_GPIO(cjtag3, GPIO_INPUT);

	if (!device_is_ready(cdc_dev) || !device_is_ready(uart_dev)) {
		return 1;
	}

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	enable_usb_device_next();
	k_sem_take(&dtr_sem, K_FOREVER);
#else
	usb_enable(NULL);
	while (1) {
		uint32_t dtr = 0U;
		uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) break;
		k_sleep(K_MSEC(100));
	}
#endif

	ring_buf_init(&rb_uart_to_usb, sizeof(ring_uart_to_usb), ring_uart_to_usb);
	ring_buf_init(&rb_usb_to_uart, sizeof(ring_usb_to_uart), ring_usb_to_uart);

	uart_irq_callback_set(cdc_dev, cdc_interrupt_handler);
	uart_irq_rx_enable(cdc_dev);

	uart_irq_callback_set(uart_dev, uart_interrupt_handler);
	uart_irq_rx_enable(uart_dev);

	while (1) {
		gpio_pin_toggle_dt(&led0);
		k_msleep(SLEEP_TIME_MS);
	}

	return 0;
}
