/*
 * Complete Dual USB CDC-ACM Catsniffer Firmware
 * Eduardo Contreras @ Electronic Cats 2026
 *
 */
#include "catsniffer.h"
#include "fw_metadata.h"
#include "shell_commands.h"

/* lora.h (via catsniffer.h) provides the complete public LoRa/FSK API */

LOG_MODULE_REGISTER(catsniffer_main, LOG_LEVEL_INF);

#define CDC0_NODE DT_NODELABEL(cdc_acm_uart0)
#define CDC1_NODE DT_NODELABEL(cdc_acm_uart1)
#define CDC2_NODE DT_NODELABEL(cdc_acm_uart2)

// Device definitions
#define CC1352_UART DEVICE_DT_GET(DT_CHOSEN(uart_cc1352))
#define CDC0_DEV DEVICE_DT_GET(CDC0_NODE)
#define CDC1_DEV DEVICE_DT_GET(CDC1_NODE)
#define CDC2_DEV DEVICE_DT_GET(CDC2_NODE)

// Communication buffers
uint8_t ring_cc1352_to_usb[RING_BUF_SIZE_BRIDGE];
uint8_t ring_usb_to_cc1352[RING_BUF_SIZE_BRIDGE];
uint8_t ring_sx1262_to_usb[RING_BUF_SIZE_LORA];
uint8_t ring_usb_to_sx1262[RING_BUF_SIZE_LORA];
uint8_t ring_config_to_usb[RING_BUF_SIZE_SHELL];
uint8_t ring_usb_to_config[RING_BUF_SIZE_SHELL];

struct ring_buf rb_cc1352_to_usb;
struct ring_buf rb_usb_to_cc1352;
struct ring_buf rb_sx1262_to_usb;
struct ring_buf rb_usb_to_sx1262;
struct ring_buf rb_config_to_usb;
struct ring_buf rb_usb_to_config;

// Device references
const struct device *uart_cc1352;
const struct device *cdc0_dev;
const struct device *cdc1_dev;
const struct device *cdc2_dev;

// Global catsniffer instance
catsniffer_t catsniffer = { 0 };

// GPIO for CC1352 control
static const struct gpio_dt_spec pin_reset =
	GPIO_DT_SPEC_GET(DT_ALIAS(pin_reset), gpios);
static const struct gpio_dt_spec pin_boot =
	GPIO_DT_SPEC_GET(DT_ALIAS(pin_boot), gpios);
// GPIO for LED control
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

// GPIO for RF switch
static const struct gpio_dt_spec ctf1 = GPIO_DT_SPEC_GET(DT_ALIAS(ctf1), gpios);
static const struct gpio_dt_spec ctf2 = GPIO_DT_SPEC_GET(DT_ALIAS(ctf2), gpios);
static const struct gpio_dt_spec ctf3 = GPIO_DT_SPEC_GET(DT_ALIAS(ctf3), gpios);

// LED array for cycling animation
const struct gpio_dt_spec *LEDs[3] = { &led2, &led0, &led1 };

// USB context
static struct usbd_context *catsniffer_usbd;

static const struct device *lora_dev;

// Thread definitions, minimized for the SAMD21 16 KB RAM
#define LORA_THREAD_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(lora_thread_stack, LORA_THREAD_STACK_SIZE);
struct k_thread lora_thread;
K_SEM_DEFINE(lora_data_sem, 0, 1);
static bool lora_async_rx_active;
static bool fsk_async_rx_active;

// Helpers for ring buffers
static inline uint32_t safe_ring_buf_put(struct ring_buf *rb,
					 const uint8_t *data, uint32_t size)
{
	unsigned int key = irq_lock();
	uint32_t result = ring_buf_put(rb, data, size);
	irq_unlock(key);
	return result;
}

static inline uint32_t safe_ring_buf_get(struct ring_buf *rb, uint8_t *data,
					 uint32_t size)
{
	unsigned int key = irq_lock();
	uint32_t result = ring_buf_get(rb, data, size);
	irq_unlock(key);
	return result;
}

void lora_rx_cb(const struct device *dev, uint8_t *data, uint16_t size,
		int16_t rssi, int8_t snr, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	/* If LoRa callback is hit while we are in FSK mode, force stop stale
	 * RX. */
	if (catsniffer.current_modulation == LORA_MOD_FSK) {
		const char *warn_msg = "WARN: LORA RX callback active in FSK "
				       "mode, forcing RX stop\r\n";
		safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t *)warn_msg,
				  strlen(warn_msg));
		if (cdc1_dev) {
			uart_irq_tx_enable(cdc1_dev);
		}

		lora_recv_async(lora_dev, NULL, NULL);
		lora_recv_async(lora_dev, NULL, NULL);
		lora_async_rx_active = false;
		fsk_async_rx_active = false;
		return;
	}

	char rx_msg[384];

	// Create hex string of the data
	char data_str[128] = { 0 };
	int display_len = (size > 40) ? 40 : size;

	for (int i = 0; i < display_len; i++) {
		char byte_str[4];
		snprintf(byte_str, sizeof(byte_str), "%02X", data[i]);
		strcat(data_str, byte_str);
	}
	if (size > 40)
		strcat(data_str, "...");

	snprintf(rx_msg, sizeof(rx_msg), "LORA RX: %s | RSSI: %d | SNR: %d\r\n",
		 data_str, rssi, snr);
	safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t *)rx_msg, strlen(rx_msg));
	if (cdc1_dev)
		uart_irq_tx_enable(cdc1_dev);
}

static void fsk_rx_cb(const struct device *dev, uint8_t *data, uint16_t size,
		      int16_t rssi, int8_t snr, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(snr);
	ARG_UNUSED(user_data);

	/* Ignore stale callback after switching back to LoRa mode. */
	if (catsniffer.current_modulation != LORA_MOD_FSK) {
		return;
	}

	char data_str[128] = { 0 };
	int display_len = (size > 40) ? 40 : (int)size;

	for (int i = 0; i < display_len; i++) {
		char byte_str[4];
		snprintf(byte_str, sizeof(byte_str), "%02X", data[i]);
		strcat(data_str, byte_str);
	}
	if (size > 40) {
		strcat(data_str, "...");
	}

	char rx_msg[384];
	snprintf(rx_msg, sizeof(rx_msg), "FSK RX: %s | RSSI: %d | Len: %d\r\n",
		 data_str, rssi, size);
	safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t *)rx_msg, strlen(rx_msg));
	if (cdc1_dev) {
		uart_irq_tx_enable(cdc1_dev);
	}
}

// USB message callback
static void catsniffer_usb_msg_cb(struct usbd_context *const ctx,
				  const struct usbd_msg *msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(ctx);
		} else if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(ctx);
		}
	}
}

// Enable USB device
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

// CC1352 UART RX: DMA-driven async reception.
//
// The SAM0 SERCOM has no RX FIFO and at 921600 baud a byte arrives every
// 11 us, which a per-byte interrupt path cannot sustain on a 48 MHz M0+.
// The fork's continuous mode runs a cyclic DMA over one buffer (no restart
// at the block end) and reports progress every tick; the callback moves
// each ready range into the bridge ring buffer. TX stays interrupt driven.
#define CC1352_RX_BUF 256
#define CC1352_RX_TIMEOUT_US 4000 /* flush tick = 1 ms (continuous DMA) */

static uint8_t cc1352_rx_buf[CC1352_RX_BUF];

static void cc1352_uart_async_cb(const struct device *dev,
				 struct uart_event *evt, void *user_data)
{
	switch (evt->type) {
	case UART_RX_RDY: {
		uint32_t written = safe_ring_buf_put(
			&rb_cc1352_to_usb,
			evt->data.rx.buf + evt->data.rx.offset,
			evt->data.rx.len);
		if (written < evt->data.rx.len) {
			catsniffer.ring_overflow_count +=
				(evt->data.rx.len - written);
		}
		uart_irq_tx_enable(cdc0_dev);
		break;
	}
	case UART_RX_BUF_REQUEST:
		/* not used with the cyclic single-buffer mode */
		break;
	case UART_RX_STOPPED:
		trace_event(TR_CB_STOP);
		if (evt->data.rx_stop.reason & UART_ERROR_OVERRUN) {
			catsniffer.uart_overrun_count++;
		}
		if (evt->data.rx_stop.reason & UART_ERROR_FRAMING) {
			/* driver reported a DMA progress regression */
			catsniffer.dma_regress_count++;
		}
		break;
	case UART_RX_DISABLED:
		trace_event(TR_CB_DIS);
		/* Restart reception unless a reconfiguration is in progress */
		if (catsniffer.baud != 0) {
			uart_rx_enable(dev, cc1352_rx_buf, CC1352_RX_BUF,
				       CC1352_RX_TIMEOUT_US);
		}
		break;
	default:
		break;
	}
}

static int cc1352_rx_start(void)
{
	return uart_rx_enable(uart_cc1352, cc1352_rx_buf, CC1352_RX_BUF,
			      CC1352_RX_TIMEOUT_US);
}

// CC1352 UART interrupt handler (TX only, RX is handled by DMA)
static void cc1352_uart_interrupt_handler(const struct device *dev,
					  void *user_data)
{
	trace_event(TR_TX_ISR);
	if (uart_irq_update(dev) && uart_irq_tx_ready(dev)) {
		uint8_t byte;

		if (safe_ring_buf_get(&rb_usb_to_cc1352, &byte, 1) == 1) {
			uart_fifo_fill(dev, &byte, 1);
			trace_event(TR_TX_FILL);
		} else {
			uart_irq_tx_disable(dev);
			trace_event(TR_TX_DIS);
		}
	}
	trace_event(TR_TX_EXIT);
}

// CDC0 (CC1352) interrupt handler - PURE BRIDGE
static void cdc0_interrupt_handler(const struct device *dev, void *user_data)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64];
			int len = uart_fifo_read(dev, buf, sizeof(buf));
			for (int i = 0; i < len; i++) {
				uint8_t data = buf[i];
				safe_ring_buf_put(&rb_usb_to_cc1352, &data, 1);
				trace_event(TR_CDC0_TX);
				uart_irq_tx_enable(uart_cc1352);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64];
			int len = safe_ring_buf_get(&rb_cc1352_to_usb, buf,
						    sizeof(buf));
			if (len > 0) {
				uart_fifo_fill(dev, buf, len);
			} else {
				uart_irq_tx_disable(dev);
			}
		}
	}
}

// CDC2 (Config/Debug) interrupt handler - TEXT SHELL
// Runs on the USB work queue: only echo and queue bytes. Commands are
// parsed and executed by the main thread (see shell_poll()).
static void cdc2_interrupt_handler(const struct device *dev, void *user_data)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[32];
			int len = uart_fifo_read(dev, buf, sizeof(buf));

			if (len > 0) {
				// Echo back for terminal feeling
				safe_ring_buf_put(&rb_config_to_usb, buf, len);
				uart_irq_tx_enable(dev);
				safe_ring_buf_put(&rb_usb_to_config, buf, len);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64];
			int len = safe_ring_buf_get(&rb_config_to_usb, buf,
						    sizeof(buf));
			if (len > 0) {
				uart_fifo_fill(dev, buf, len);
			} else {
				uart_irq_tx_disable(dev);
			}
		}
	}
}

// Main-thread shell parser: line-buffer queued bytes and run commands
static void shell_poll(void)
{
	uint8_t data;

	while (safe_ring_buf_get(&rb_usb_to_config, &data, 1) == 1) {
		if (data == '\n' || data == '\r') {
			if (catsniffer.command_data_len > 0) {
				catsniffer.command_data
					[catsniffer.command_data_len] = '\0';
				process_command(catsniffer.command_data,
						catsniffer.command_data_len);
				catsniffer.command_data_len = 0;
			}
		} else if (catsniffer.command_data_len < COMMAND_BUF_SIZE - 1) {
			catsniffer.command_data[catsniffer.command_data_len++] =
				data;
		}
	}
}

// CDC1 (SX1262) interrupt handler
static void cdc1_interrupt_handler(const struct device *dev, void *user_data)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64];
			int len = uart_fifo_read(dev, buf, sizeof(buf));
			if (len > 0) {
				safe_ring_buf_put(&rb_usb_to_sx1262, buf, len);
				k_sem_give(&lora_data_sem); // Wake up LoRa
							    // thread
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64];

			int len = safe_ring_buf_get(&rb_sx1262_to_usb, buf,
						    sizeof(buf));
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
	if (new_baud == catsniffer.baud)
		return;

	trace_event(TR_BAUD_0);
	uart_irq_tx_disable(uart_cc1352);
	/* baud == 0 tells the async callback not to auto-restart RX */
	catsniffer.baud = 0;
	uart_rx_disable(uart_cc1352);
	k_msleep(2);
	trace_event(TR_BAUD_1);

	struct uart_config cfg;
	uart_config_get(uart_cc1352, &cfg);
	cfg.baudrate = new_baud;
	uart_configure(uart_cc1352, &cfg);
	catsniffer.baud = new_baud;
	trace_event(TR_BAUD_2);

	cc1352_rx_start();
	trace_event(TR_BAUD_3);
}

void change_band(unsigned long new_band)
{
	if (new_band == catsniffer.band)
		return;

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
	if (new_mode == catsniffer.mode)
		return;

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

// Shell Helpers (Exposed to shell_commands.c) ---

void shell_reply(const char *msg)
{
	size_t len;
	int wait_ms = 0;

	if (!msg)
		return;
	len = strlen(msg);
	/* The shell ring is small on this board (RING_BUF_SIZE_SHELL), so a
	 * reply may be larger than the ring. Write it in pieces and wait for
	 * the USB side to drain in between (thread context only, bounded).
	 */
	while (len > 0) {
		uint32_t space = ring_buf_space_get(&rb_config_to_usb);

		if (space == 0) {
			if (k_is_in_isr() || wait_ms >= 500) {
				trace_event(TR_WAIT_CAP);
				return;
			}
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			k_msleep(1);
			wait_ms++;
			continue;
		}
		if (space > len)
			space = len;
		safe_ring_buf_put(&rb_config_to_usb, (const uint8_t *)msg,
				  space);
		msg += space;
		len -= space;
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
	}
}

int queue_radio_command(const char *cmd_line)
{
	uint8_t nl = '\n';
	size_t len;
	uint32_t written;

	if (!cmd_line) {
		return -EINVAL;
	}

	len = strlen(cmd_line);
	if (len == 0 || len >= COMMAND_BUF_SIZE) {
		return -EINVAL;
	}

	written = safe_ring_buf_put(&rb_usb_to_sx1262,
				    (const uint8_t *)cmd_line, len);
	if (written != len) {
		return -ENOSPC;
	}

	written = safe_ring_buf_put(&rb_usb_to_sx1262, &nl, 1);
	if (written != 1) {
		return -ENOSPC;
	}

	k_sem_give(&lora_data_sem);
	return 0;
}

void set_status_leds(int l0, int l1, int l2)
{
	gpio_pin_set_dt(&led0, l0);
	gpio_pin_set_dt(&led1, l1);
	gpio_pin_set_dt(&led2, l2);
}

// Forward declarations
static const char *get_error_string(int error);
static int lora_set_tx_mode(void);
static void lora_stop_rx(void);
static int lora_start_rx_async(void);
static void fsk_stop_rx(void);
static int fsk_start_rx_async(void);

int initialize_lora(void)
{
	const char *status_msg;

	if (catsniffer.lora_initialized) {
		status_msg = "LoRa: Already initialized\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return 0;
	}

	status_msg = "LoRa: Starting initialization...\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
	if (!device_is_ready(lora_dev)) {
		status_msg = "ERROR: LoRa device not ready\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return -ENODEV;
	}

	status_msg = "LoRa: Device ready\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	struct lora_modem_config config = { 0 };
	config.frequency = catsniffer.lora_config.frequency;
	config.bandwidth = catsniffer.lora_config.bandwidth;
	config.datarate = catsniffer.lora_config.spreading_factor;
	config.preamble_len = catsniffer.lora_config.preamble_len;
	config.coding_rate = catsniffer.lora_config.coding_rate;
	config.tx_power = catsniffer.lora_config.tx_power;
	config.tx = false;
	config.iq_inverted = catsniffer.lora_config.iq_inverted;
	config.public_network = catsniffer.lora_config.public_network;
	config.lora_sync_word = catsniffer.lora_config.lora_sync_word;

	int ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		char err_buf[64];
		snprintf(err_buf, sizeof(err_buf),
			 "ERROR: LoRa config failed: %d\r\n", ret);
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)err_buf,
				  strlen(err_buf));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return ret;
	}

	catsniffer.lora_initialized = true;
	catsniffer.lora_config_lock = false;

	/* Arm LoRa RX immediately on init, not only in thread periodic loop. */
	ret = lora_start_rx_async();
	if (ret < 0 && ret != -EBUSY) {
		char err_buf[96];
		snprintf(err_buf, sizeof(err_buf),
			 "WARN: LoRa RX start failed (%d: %s)\r\n", ret,
			 get_error_string(ret));
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)err_buf,
				  strlen(err_buf));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
	}

	status_msg = "LoRa: Initialization completed (RX mode)!\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	return 0;
}

int apply_lora_config(void)
{
	const char *status_msg;

	if (!catsniffer.lora_initialized) {
		status_msg = "Error: LoRa not initialized. Use TEST command "
			     "first.\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return -ENODEV;
	}

	// Lock to prevent LoRa thread from interfering
	catsniffer.lora_config_lock = true;
	k_msleep(50); // Wait for any ongoing operation to complete

	// Free radio from Rx
	lora_recv_async(lora_dev, NULL, NULL);
	lora_async_rx_active = false;

	status_msg = "Applying LoRa configuration...\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	struct lora_modem_config config = { 0 };
	config.frequency = catsniffer.lora_config.frequency;
	config.bandwidth = catsniffer.lora_config.bandwidth;
	config.datarate = catsniffer.lora_config.spreading_factor;
	config.preamble_len = catsniffer.lora_config.preamble_len;
	config.coding_rate = catsniffer.lora_config.coding_rate;
	config.tx_power = catsniffer.lora_config.tx_power;
	config.tx = false;
	config.iq_inverted = catsniffer.lora_config.iq_inverted;
	config.public_network = catsniffer.lora_config.public_network;
	config.lora_sync_word = catsniffer.lora_config.lora_sync_word;

	int ret = lora_config(lora_dev, &config);

	// Unlock regardless of result
	catsniffer.lora_config_lock = false;

	if (ret < 0) {
		char err_buf[96];
		snprintf(err_buf, sizeof(err_buf),
			 "ERROR: LoRa configuration failed (%d: %s)\r\n", ret,
			 get_error_string(ret));
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)err_buf,
				  strlen(err_buf));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return ret;
	}

	status_msg = "LoRa configuration applied successfully (RX mode)\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	/* Re-arm RX right after successful apply. */
	ret = lora_start_rx_async();
	if (ret < 0 && ret != -EBUSY) {
		char err_buf[96];
		snprintf(err_buf, sizeof(err_buf),
			 "WARN: LoRa RX re-arm failed (%d: %s)\r\n", ret,
			 get_error_string(ret));
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)err_buf,
				  strlen(err_buf));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
	}

	return 0;
}

/* ============================================ */
/* FSK/GFSK Functions                           */
/* ============================================ */

/* Convert enum lora_fsk_bandwidth (nominal kHz) to Hz for guard checks. */
static uint32_t fsk_bw_enum_to_hz(enum lora_fsk_bandwidth bw)
{
	/* Each enum value is the nominal bandwidth in kHz; multiply by 1000.
	 * This is a close approximation (e.g. FSK_BW_117_KHZ=117 -> 117000 Hz
	 * vs. the exact 117300 Hz), which is accurate enough for the sanity
	 * guard below.
	 */
	return (uint32_t)bw * 1000U;
}

int apply_fsk_config(void)
{
	const char *status_msg;

	if (!lora_dev) {
		lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
		if (!device_is_ready(lora_dev)) {
			status_msg = "Error: SX126x device not ready\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return -ENODEV;
		}
	}

	/* Lock modem operations while changing modulation/config */
	catsniffer.lora_config_lock = true;

	/* Stop any ongoing RX activity before reconfiguration */
	lora_recv_async(lora_dev, NULL, NULL);
	fsk_async_rx_active = false;
	lora_recv_async(lora_dev, NULL, NULL);
	lora_async_rx_active = false;
	k_msleep(10);

	status_msg = "Applying FSK configuration...\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	/* Guard against impossible BW selections for configured bitrate/fdev.
	 */
	uint32_t bw_hz = fsk_bw_enum_to_hz(catsniffer.fsk_config.bandwidth);
	uint32_t required_hz = catsniffer.fsk_config.bitrate +
			       (2 * catsniffer.fsk_config.fdev);
	if (bw_hz < required_hz) {
		catsniffer.fsk_config.bandwidth = FSK_BW_187_KHZ;
		char warn_buf[128];
		snprintf(warn_buf, sizeof(warn_buf),
			 "WARN: FSK BW too narrow (%lu Hz < %lu Hz), forcing "
			 "FSK_BW_187_KHZ\r\n",
			 (unsigned long)bw_hz, (unsigned long)required_hz);
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)warn_buf,
				  strlen(warn_buf));
		if (cdc2_dev) {
			uart_irq_tx_enable(cdc2_dev);
		}
	}

	/* Configure FSK using the public lora_config() API */
	fsk_config_t *f = &catsniffer.fsk_config;
	struct lora_modem_config cfg = { 0 };

	cfg.modulation = LORA_MOD_FSK;
	cfg.frequency = f->frequency;
	cfg.tx_power = f->tx_power;
	cfg.tx = false; /* start in RX mode */
	cfg.fsk.bitrate = f->bitrate;
	cfg.fsk.fdev = f->fdev;
	cfg.fsk.shaping = f->shaping;
	cfg.fsk.bandwidth = f->bandwidth;
	cfg.fsk.preamble_len = f->preamble_len;
	memcpy(cfg.fsk.sync_word, f->sync_word, f->sync_word_len);
	cfg.fsk.sync_word_len = f->sync_word_len;
	cfg.fsk.variable_len = !f->fixed_length;
	cfg.fsk.payload_len = f->payload_len;
	cfg.fsk.crc_on = f->crc_on;
	cfg.fsk.whitening = f->whitening;

	int ret = lora_config(lora_dev, &cfg);
	if (ret < 0) {
		char err_buf[96];
		snprintf(err_buf, sizeof(err_buf),
			 "ERROR: FSK lora_config failed (%d)\r\n", ret);
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)err_buf,
				  strlen(err_buf));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		catsniffer.lora_config_lock = false;
		return ret;
	}

	catsniffer.fsk_initialized = true;
	catsniffer.current_modulation = LORA_MOD_FSK;
	catsniffer.lora_initialized = false;
	catsniffer.lora_config_lock = false;

	/* Match LoRa UX: automatically arm continuous FSK RX after config. */
	ret = fsk_start_rx_async();
	if (ret < 0 && ret != -EBUSY) {
		char warn_buf[96];
		snprintf(warn_buf, sizeof(warn_buf),
			 "WARN: FSK RX auto-start failed (%d)\r\n", ret);
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)warn_buf,
				  strlen(warn_buf));
		if (cdc2_dev) {
			uart_irq_tx_enable(cdc2_dev);
		}
	}

	status_msg = "FSK configuration applied successfully\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);

	return 0;
}

int switch_to_lora(void)
{
	const char *status_msg;

	if (!lora_dev) {
		lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
		if (!device_is_ready(lora_dev)) {
			return -ENODEV;
		}
	}

	/* Ensure FSK async RX is not left active while switching modes. */
	fsk_stop_rx();

	catsniffer.current_modulation = LORA_MOD_LORA;
	catsniffer.lora_initialized = true;

	/* Calling lora_config() with modulation=LORA_MOD_LORA (the default
	 * when zero-initialised) switches the driver back to LoRa mode and
	 * applies the stored parameters in one step. */
	int ret = apply_lora_config();
	if (ret < 0) {
		catsniffer.lora_initialized = false;
		status_msg = "Error switching to LoRa mode\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
	}

	return ret;
}

int switch_to_fsk(void)
{
	/* Ensure LoRa async RX is not left active while switching modes. */
	lora_stop_rx();
	catsniffer.lora_initialized = false;
	/* Apply FSK config which will switch modulation */
	return apply_fsk_config();
}

static const char *get_error_string(int error)
{
	switch (error) {
	case 0:
		return "Success";
	case -EAGAIN:
		return "EAGAIN - Resource temporarily unavailable";
	case -EBUSY:
		return "EBUSY - Device busy";
	case -EIO:
		return "EIO - I/O error";
	case -EINVAL:
		return "EINVAL - Invalid argument";
	case -ENODEV:
		return "ENODEV - Device not ready";
	case -ENOTSUP:
		return "ENOTSUP - Operation not supported";
	case -ETIMEDOUT:
		return "ETIMEDOUT - Timeout";
	default:
		return "Unknown error";
	}
}

// Exposed for shell_commands.c
void process_lora_command(char *cmd_line)
{
	char response[384];
	const char *status_msg;

	/* Check if we're in FSK mode */
	bool is_fsk = (catsniffer.current_modulation == LORA_MOD_FSK);

	if (!catsniffer.lora_initialized && !is_fsk) {
		int ret = initialize_lora();
		if (ret < 0)
			return;
	}

	if (strncmp(cmd_line, "TEST", 4) == 0) {
		if (is_fsk) {
			status_msg = "TEST: FSK mode active\r\n";
		} else {
			status_msg = "TEST: LoRa ready!\r\n";
		}
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return;
	}

	/* FSK TX Test */
	if (strncmp(cmd_line, "FSKTEST", 7) == 0) {
		if (!catsniffer.fsk_initialized) {
			status_msg = "ERROR: FSK not initialized. Use "
				     "fsk_apply first.\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		uint8_t tx_data[] = "FSK_PING";
		status_msg = "FSK: Sending test packet...\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);

		bool was_rx_active = fsk_async_rx_active;
		if (was_rx_active) {
			fsk_stop_rx();
		}

		int ret = lora_send(lora_dev, tx_data, sizeof(tx_data) - 1);
		if (was_rx_active) {
			int rx_ret = fsk_start_rx_async();
			if (rx_ret < 0) {
				char warn_buf[96];
				snprintf(warn_buf, sizeof(warn_buf),
					 "WARN: FSK RX re-arm failed: %s\r\n",
					 get_error_string(rx_ret));
				safe_ring_buf_put(&rb_config_to_usb,
						  (uint8_t *)warn_buf,
						  strlen(warn_buf));
				if (cdc2_dev) {
					uart_irq_tx_enable(cdc2_dev);
				}
			}
		}

		snprintf(response, sizeof(response),
			 "FSK TX Result: %d (%s)\r\n", ret,
			 get_error_string(ret));
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)response,
				  strlen(response));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return;
	}

	/* FSK TX with hex data */
	if (strncmp(cmd_line, "FSKTX ", 6) == 0) {
		if (!catsniffer.fsk_initialized) {
			status_msg = "ERROR: FSK not initialized. Use "
				     "fsk_apply first.\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		char *hex_data = &cmd_line[6];
		uint8_t tx_data[128];
		size_t hex_len = strlen(hex_data);

		if (hex_len % 2 == 0 && hex_len > 0) {
			size_t data_len = hex_len / 2;

			for (size_t i = 0; i < data_len; i++) {
				char hex_byte[3] = { hex_data[i * 2],
						     hex_data[i * 2 + 1],
						     '\0' };
				tx_data[i] =
					(uint8_t)strtoul(hex_byte, NULL, 16);
			}

			bool was_rx_active = fsk_async_rx_active;
			if (was_rx_active) {
				fsk_stop_rx();
			}

			int ret = lora_send(lora_dev, tx_data, data_len);
			if (was_rx_active) {
				int rx_ret = fsk_start_rx_async();
				if (rx_ret < 0) {
					char warn_buf[96];
					snprintf(warn_buf, sizeof(warn_buf),
						 "WARN: FSK RX re-arm failed: "
						 "%s\r\n",
						 get_error_string(rx_ret));
					safe_ring_buf_put(&rb_config_to_usb,
							  (uint8_t *)warn_buf,
							  strlen(warn_buf));
					if (cdc2_dev) {
						uart_irq_tx_enable(cdc2_dev);
					}
				}
			}
			snprintf(response, sizeof(response),
				 "FSK TX Result: %d (%s)\r\n", ret,
				 get_error_string(ret));
		} else {
			snprintf(response, sizeof(response),
				 "ERROR: Invalid hex data\r\n");
		}

		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)response,
				  strlen(response));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return;
	}

	/* FSK RX command */
	if (strncmp(cmd_line, "FSKRX", 5) == 0) {
		if (!catsniffer.fsk_initialized) {
			status_msg = "ERROR: FSK not initialized.\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		/* RadioLib-style: keep continuous async FSK RX armed on DIO1
		 * IRQ. */
		int ret = fsk_start_rx_async();
		if (ret == 0) {
			snprintf(response, sizeof(response),
				 "FSK RX async active\r\n");
		} else if (ret == -EBUSY && fsk_async_rx_active) {
			snprintf(response, sizeof(response),
				 "FSK RX async already active\r\n");
		} else {
			snprintf(response, sizeof(response),
				 "FSK RX start failed: %s\r\n",
				 get_error_string(ret));
		}

		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)response,
				  strlen(response));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return;
	}

	if (strncmp(cmd_line, "FSKRXSTOP", 9) == 0) {
		fsk_stop_rx();
		status_msg = "FSK RX stopped\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev) {
			uart_irq_tx_enable(cdc2_dev);
		}
		return;
	}

	if (strncmp(cmd_line, "TXTEST", 6) == 0) {
		if (is_fsk) {
			status_msg = "ERROR: Modulation is FSK. Use "
				     "FSKTEST/FSKTX or switch to LoRa.\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		if (!catsniffer.lora_initialized) {
			status_msg = "ERROR: LoRa not initialized\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		uint8_t tx_data[] = "PING";
		status_msg = "DEBUG: Sending PING packet\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
				  strlen(status_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);

		lora_stop_rx();
		lora_set_tx_mode();
		int ret = lora_send(lora_dev, tx_data, sizeof(tx_data));
		lora_start_rx_async();

		snprintf(response, sizeof(response), "TX Result: %d (%s)\r\n",
			 ret, get_error_string(ret));
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)response,
				  strlen(response));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return;
	}

	if (strncmp(cmd_line, "TX ", 3) == 0) {
		if (is_fsk) {
			status_msg = "ERROR: Modulation is FSK. Use FSKTX or "
				     "switch to LoRa.\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		if (!catsniffer.lora_initialized) {
			status_msg = "ERROR: LoRa not initialized\r\n";
			safe_ring_buf_put(&rb_config_to_usb,
					  (uint8_t *)status_msg,
					  strlen(status_msg));
			if (cdc2_dev)
				uart_irq_tx_enable(cdc2_dev);
			return;
		}

		char *hex_data = &cmd_line[3];
		uint8_t tx_data[128];
		size_t hex_len = strlen(hex_data);

		if (hex_len % 2 == 0 && hex_len > 0) {
			size_t data_len = hex_len / 2;

			for (size_t i = 0; i < data_len; i++) {
				char hex_byte[3] = { hex_data[i * 2],
						     hex_data[i * 2 + 1],
						     '\0' };
				tx_data[i] =
					(uint8_t)strtoul(hex_byte, NULL, 16);
			}

			lora_stop_rx();
			lora_set_tx_mode();
			int ret = lora_send(lora_dev, tx_data, data_len);
			lora_start_rx_async();

			snprintf(response, sizeof(response),
				 "TX Result: %d (%s)\r\n", ret,
				 get_error_string(ret));
		} else {
			snprintf(response, sizeof(response),
				 "ERROR: Invalid hex data\r\n");
		}

		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)response,
				  strlen(response));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
		return;
	}

	status_msg = "Available: TEST, TX <hex>, FSKTEST, FSKTX <hex>, FSKRX, "
		     "FSKRXSTOP\r\n";
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)status_msg,
			  strlen(status_msg));
	if (cdc2_dev)
		uart_irq_tx_enable(cdc2_dev);
}

// Detiene explicitamente la recepcion asincrona
static void lora_stop_rx(void)
{
	// Pasar NULL y NULL cancela la recepcion asincrona en Zephyr
	lora_recv_async(lora_dev, NULL, NULL);
	lora_async_rx_active = false;

	// IMPORTANTE: Dar un pequeno respiro al bus SPI/Driver para cambiar de
	// estado
	k_sleep(K_MSEC(5));
}

// Helper function to configure LoRa for TX
static int lora_set_tx_mode(void)
{
	struct lora_modem_config config = { 0 };
	config.frequency = catsniffer.lora_config.frequency;
	config.bandwidth = catsniffer.lora_config.bandwidth;
	config.datarate = catsniffer.lora_config.spreading_factor;
	config.preamble_len = catsniffer.lora_config.preamble_len;
	config.coding_rate = catsniffer.lora_config.coding_rate;
	config.tx_power = catsniffer.lora_config.tx_power;
	config.tx = true;
	config.iq_inverted = catsniffer.lora_config.iq_inverted;
	config.public_network = catsniffer.lora_config.public_network;
	config.lora_sync_word = catsniffer.lora_config.lora_sync_word;
	return lora_config(lora_dev, &config);
}

// Helper function to start LoRa async RX (radio must already be configured)
static int lora_start_rx_async(void)
{
	if (lora_async_rx_active) {
		return 0;
	}

	int ret = lora_recv_async(lora_dev, lora_rx_cb, NULL);
	if (ret == 0) {
		lora_async_rx_active = true;
		return 0;
	}

	/* Recover if the driver is left busy from a prior state transition. */
	if (ret == -EBUSY) {
		lora_recv_async(lora_dev, NULL, NULL);
		k_sleep(K_MSEC(2));
		ret = lora_recv_async(lora_dev, lora_rx_cb, NULL);
		if (ret == 0) {
			lora_async_rx_active = true;
		}
	}
	return ret;
}

/* Stop FSK RX */
static void fsk_stop_rx(void)
{
	lora_recv_async(lora_dev, NULL, NULL);
	fsk_async_rx_active = false;
	k_sleep(K_MSEC(5));
}

/* Start FSK async RX in continuous mode */
static int fsk_start_rx_async(void)
{
	if (fsk_async_rx_active) {
		return 0;
	}

	if (!catsniffer.fsk_initialized ||
	    catsniffer.current_modulation != LORA_MOD_FSK) {
		return -EINVAL;
	}

	int ret = lora_recv_async(lora_dev, fsk_rx_cb, NULL);
	if (ret == 0) {
		fsk_async_rx_active = true;
		return 0;
	}

	/* Recover if driver state was left busy but RX is not actually active.
	 */
	if (ret == -EBUSY) {
		lora_recv_async(lora_dev, NULL, NULL);
		k_sleep(K_MSEC(2));
		ret = lora_recv_async(lora_dev, fsk_rx_cb, NULL);
		if (ret == 0) {
			fsk_async_rx_active = true;
		}
	}

	return ret;
}

// LoRa/FSK thread function
static void lora_thread_func(void *p1, void *p2, void *p3)
{
	char command_buffer[128];
	size_t cmd_len = 0;

	while (1) {
		/* Wake on USB activity or periodic timeout to keep RX armed. */
		(void)k_sem_take(&lora_data_sem, K_MSEC(100));
		// Skip operations if config lock is active
		if (catsniffer.lora_config_lock) {
			k_msleep(10);
			continue;
		}

		/* Handle based on current modulation */
		if (catsniffer.current_modulation == LORA_MOD_FSK &&
		    catsniffer.fsk_initialized) {
			/* Keep FSK RX armed in both command and stream modes.
			 */
			(void)fsk_start_rx_async();
			if (catsniffer.lora_mode == LORA_MODE_COMMAND) {
				/* FSK Command Mode */
				uint8_t usb_buf[64];
				int usb_len = safe_ring_buf_get(
					&rb_usb_to_sx1262, usb_buf,
					sizeof(usb_buf));

				if (usb_len > 0) {
					for (int i = 0; i < usb_len; i++) {
						char c = usb_buf[i];
						if (c == '\n' || c == '\r') {
							if (cmd_len > 0) {
								command_buffer
									[cmd_len] =
										'\0';
								process_lora_command(
									command_buffer);
								cmd_len = 0;
							}
						} else if (cmd_len <
							   sizeof(command_buffer) -
								   1) {
							command_buffer[cmd_len++] =
								c;
						}
					}
				}

				/* Keep async FSK RX armed in command mode. */
			} else {
				/* FSK Stream Mode:
				 * Keep async RX armed continuously; only pause
				 * for TX.
				 */
				uint8_t tx_buffer[255];
				int tx_len = safe_ring_buf_get(
					&rb_usb_to_sx1262, tx_buffer,
					sizeof(tx_buffer));

				if (tx_len > 0) {
					if (fsk_async_rx_active) {
						fsk_stop_rx();
					}
					int ret = lora_send(lora_dev, tx_buffer,
							    tx_len);

					if (ret < 0) {
						char err_msg[64];
						snprintf(err_msg,
							 sizeof(err_msg),
							 "FSK TX Error: %d\r\n",
							 ret);
						safe_ring_buf_put(
							&rb_sx1262_to_usb,
							(uint8_t *)err_msg,
							strlen(err_msg));
						if (cdc1_dev)
							uart_irq_tx_enable(
								cdc1_dev);
					}
					(void)fsk_start_rx_async();
				} else {
					int ret = fsk_start_rx_async();
					if (ret < 0 && ret != -EBUSY) {
						char err_msg[96];
						snprintf(err_msg,
							 sizeof(err_msg),
							 "FSK RX async start "
							 "error: %d (%s)\r\n",
							 ret,
							 get_error_string(ret));
						safe_ring_buf_put(
							&rb_sx1262_to_usb,
							(uint8_t *)err_msg,
							strlen(err_msg));
						if (cdc1_dev) {
							uart_irq_tx_enable(
								cdc1_dev);
						}
					}
				}
			}
		} else if (catsniffer.current_modulation == LORA_MOD_LORA) {
			/* LoRa Mode */
			lora_start_rx_async();
			if (catsniffer.lora_mode == LORA_MODE_COMMAND) {
				uint8_t usb_buf[64];
				int usb_len = safe_ring_buf_get(
					&rb_usb_to_sx1262, usb_buf,
					sizeof(usb_buf));

				if (usb_len > 0) {
					for (int i = 0; i < usb_len; i++) {
						char c = usb_buf[i];

						if (c == '\n' || c == '\r') {
							if (cmd_len > 0) {
								command_buffer
									[cmd_len] =
										'\0';
								process_lora_command(
									command_buffer);
								cmd_len = 0;
							}
						} else if (cmd_len <
							   sizeof(command_buffer) -
								   1) {
							command_buffer[cmd_len++] =
								c;
						}
					}
				}
			} else {
				uint8_t tx_buffer[255];
				int tx_len = safe_ring_buf_get(
					&rb_usb_to_sx1262, tx_buffer,
					sizeof(tx_buffer));

				if (tx_len > 0 && catsniffer.lora_initialized) {
					lora_stop_rx();
					lora_set_tx_mode();
					lora_send(lora_dev, tx_buffer, tx_len);
					lora_start_rx_async();
				}
			}
		}
	}
}

int main(void)
{
	int ret;
	// Get device references
	uart_cc1352 = CC1352_UART;
	cdc0_dev = CDC0_DEV;
	cdc1_dev = CDC1_DEV;
	cdc2_dev = CDC2_DEV;

	// Initialize GPIO
	INIT_GPIO(pin_reset, GPIO_OUTPUT);
	INIT_GPIO(pin_boot, GPIO_INPUT | GPIO_PULL_UP);
	INIT_GPIO(led0, GPIO_OUTPUT);
	INIT_GPIO(led1, GPIO_OUTPUT);
	INIT_GPIO(led2, GPIO_OUTPUT);
	INIT_GPIO(ctf1, GPIO_OUTPUT);
	INIT_GPIO(ctf2, GPIO_OUTPUT);
	INIT_GPIO(ctf3, GPIO_OUTPUT);

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

	// Initialize LoRa configuration defaults
	catsniffer.lora_mode = LORA_MODE_STREAM;
	catsniffer.lora_config.frequency = 915000000;
	catsniffer.lora_config.spreading_factor = SF_7;
	catsniffer.lora_config.bandwidth = BW_125_KHZ;
	catsniffer.lora_config.coding_rate = CR_4_5;
	catsniffer.lora_config.tx_power = 20;
	catsniffer.lora_config.preamble_len = 12;
	catsniffer.lora_config.iq_inverted = false;
	catsniffer.lora_config.public_network = false;
	catsniffer.lora_config.config_pending = false;
	catsniffer.lora_initialized = false;
	catsniffer.lora_config_lock = false;

	// Initialize FSK configuration defaults
	catsniffer.current_modulation = LORA_MOD_LORA;
	catsniffer.fsk_config.frequency = 915000000;
	catsniffer.fsk_config.bitrate = 50000;
	catsniffer.fsk_config.fdev = 25000;
	catsniffer.fsk_config.shaping = LORA_FSK_SHAPING_GAUSS_BT_0_5;
	catsniffer.fsk_config.bandwidth = FSK_BW_187_KHZ;
	catsniffer.fsk_config.tx_power = 14;
	catsniffer.fsk_config.preamble_len = 8;
	catsniffer.fsk_config.sync_word[0] = 0x12;
	catsniffer.fsk_config.sync_word[1] = 0xAD;
	catsniffer.fsk_config.sync_word_len = 2;
	catsniffer.fsk_config.fixed_length = false;
	catsniffer.fsk_config.payload_len = 255;
	catsniffer.fsk_config.crc_on = false;
	catsniffer.fsk_config.whitening = false;
	catsniffer.fsk_config.config_pending = false;
	catsniffer.fsk_initialized = false;

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
	ring_buf_init(&rb_cc1352_to_usb, sizeof(ring_cc1352_to_usb),
		      ring_cc1352_to_usb);
	ring_buf_init(&rb_usb_to_cc1352, sizeof(ring_usb_to_cc1352),
		      ring_usb_to_cc1352);
	ring_buf_init(&rb_sx1262_to_usb, sizeof(ring_sx1262_to_usb),
		      ring_sx1262_to_usb);
	ring_buf_init(&rb_usb_to_sx1262, sizeof(ring_usb_to_sx1262),
		      ring_usb_to_sx1262);
	ring_buf_init(&rb_config_to_usb, sizeof(ring_config_to_usb),
		      ring_config_to_usb);
	ring_buf_init(&rb_usb_to_config, sizeof(ring_usb_to_config),
		      ring_usb_to_config);

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

	if (cdc2_dev) {
		uart_irq_callback_set(cdc2_dev, cdc2_interrupt_handler);
		uart_irq_rx_enable(cdc2_dev);
	}

	uart_irq_callback_set(uart_cc1352, cc1352_uart_interrupt_handler);
	uart_callback_set(uart_cc1352, cc1352_uart_async_cb, NULL);
	ret = cc1352_rx_start();
	if (ret < 0) {
		const char *msg = "CC1352 UART RX start failed\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)msg,
				  strlen(msg));
	}

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

	// Initialize LoRa BEFORE the USB startup delay - chip is in a clean
	// state right after reset, before any DIO1 spurious interrupts from
	// USB enumeration can confuse the SX126x driver.
	for (int lora_try = 0; lora_try < 3; lora_try++) {
		ret = initialize_lora();
		if (ret == 0) {
			break;
		}
		// Brief pause to let BUSY settle before retry
		k_msleep(50);
		// Reset the initialized flag so initialize_lora() re-runs
		catsniffer.lora_initialized = false;
	}

	// Send startup message AFTER everything is initialized
	k_msleep(1000); // Wait a moment for USB to be ready
	char startup_msg[200];
	snprintf(startup_msg, sizeof(startup_msg),
		 "Catsniffer Firmware Ready [%s] - Config Port\r\n",
		 CATSNIFFER_FW_VERSION);
	safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)startup_msg,
			  strlen(startup_msg));
	uart_irq_tx_enable(cdc2_dev);

	const char *lora_welcome = "LoRa Control Port\n";
	safe_ring_buf_put(&rb_sx1262_to_usb, (uint8_t *)lora_welcome,
			  strlen(lora_welcome));
	uart_irq_tx_enable(cdc1_dev);

	if (ret < 0) {
		const char *fail_msg = "LoRa initialization failed\r\n";
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)fail_msg,
				  strlen(fail_msg));
		if (cdc2_dev)
			uart_irq_tx_enable(cdc2_dev);
	}

	{
		/* Startup diagnostics: main stack low-water mark after init */
		size_t unused = 0;
		char diag[48];

		k_thread_stack_space_get(k_current_get(), &unused);
		snprintf(diag, sizeof(diag), "Boot: main stack unused=%u\r\n",
			 (unsigned int)unused);
		safe_ring_buf_put(&rb_config_to_usb, (uint8_t *)diag,
				  strlen(diag));
	}

	// Start LoRa thread
	k_thread_create(&lora_thread, lora_thread_stack, LORA_THREAD_STACK_SIZE,
			lora_thread_func, NULL, NULL, NULL,
			LORA_THREAD_PRIORITY, 0, K_NO_WAIT);

	// Main loop: shell commands and LED animation
	while (1) {
		shell_poll();
		if (catsniffer.led_identify) {
			k_msleep(10);
			continue;
		}
		int64_t current_time = k_uptime_get();
		if (current_time - catsniffer.previous_millis >
		    catsniffer.led_interval) {
			catsniffer.previous_millis = current_time;
			// Check catsniffer mode
			if (catsniffer.mode) {
				static int led_index = 0;
				// Cycle through LEDs
				gpio_pin_toggle_dt(LEDs[led_index]);
				led_index++;
				if (led_index > 2)
					led_index = 0;
			} else {
				// Just toggle LED2
				gpio_pin_toggle_dt(&led2);
			}
		}

		k_msleep(10);
	}

	return 0;
}
