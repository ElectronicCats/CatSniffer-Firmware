/*
 * shell_commands.c - Command Table Based Shell
 */

#include "shell_commands.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "catsniffer.h"
#include "fw_metadata.h"

// External functions from main.c
void shell_reply(const char *msg);
void change_mode(unsigned long new_mode);
void change_band(unsigned long new_band);
void process_lora_command(char *cmd_line);
int queue_radio_command(const char *cmd_line);
void set_status_leds(int l0, int l1, int l2);

extern catsniffer_t catsniffer;

// Command handler type
typedef void (*cmd_handler_t)(char *args);

// Command table entry
typedef struct {
	const char *name;
	cmd_handler_t handler;
	const char *help;
	bool prefix_match; // true for commands with args (TX, TEST)
} shell_cmd_t;

// Forward declarations
static void cmd_help(char *args);
static void cmd_boot(char *args);
static void cmd_exit(char *args);
static void cmd_band1(char *args);
static void cmd_band2(char *args);
static void cmd_band3(char *args);
static void cmd_reboot(char *args);
static void cmd_status(char *args);
static void cmd_loss_reset(char *args);
static void cmd_fw_version(char *args);
static void cmd_lora_freq(char *args);
static void cmd_lora_sf(char *args);
static void cmd_lora_bw(char *args);
static void cmd_lora_cr(char *args);
static void cmd_lora_power(char *args);
static void cmd_lora_mode(char *args);
static void cmd_lora_preamble(char *args);
static void cmd_lora_syncword(char *args);
static void cmd_lora_iq(char *args);
static void cmd_lora_config(char *args);
static void cmd_lora_apply(char *args);
static void cmd_cc1352_fw_id(char *args);
static void cmd_fsk_freq(char *args);
static void cmd_fsk_bitrate(char *args);
static void cmd_fsk_fdev(char *args);
static void cmd_fsk_bw(char *args);
static void cmd_fsk_power(char *args);
static void cmd_fsk_preamble(char *args);
static void cmd_fsk_syncword(char *args);
static void cmd_fsk_crc(char *args);
static void cmd_fsk_whitening(char *args);
static void cmd_fsk_pktlen(char *args);
static void cmd_fsk_payload(char *args);
static void cmd_fsk_bt(char *args);
static void cmd_fsk_config(char *args);
static void cmd_fsk_apply(char *args);
static void cmd_modulation(char *args);
static void cmd_radio(char *args);
static void cmd_identify(char *args);

static const char *fsk_bw_enum_to_khz_str(enum lora_fsk_bandwidth bw)
{
	switch (bw) {
	case FSK_BW_4_KHZ:
		return "4.8";
	case FSK_BW_5_KHZ:
		return "5.8";
	case FSK_BW_7_KHZ:
		return "7.3";
	case FSK_BW_9_KHZ:
		return "9.7";
	case FSK_BW_11_KHZ:
		return "11.7";
	case FSK_BW_14_KHZ:
		return "14.6";
	case FSK_BW_19_KHZ:
		return "19.5";
	case FSK_BW_23_KHZ:
		return "23.4";
	case FSK_BW_29_KHZ:
		return "29.3";
	case FSK_BW_39_KHZ:
		return "39.0";
	case FSK_BW_46_KHZ:
		return "46.9";
	case FSK_BW_58_KHZ:
		return "58.6";
	case FSK_BW_78_KHZ:
		return "78.2";
	case FSK_BW_93_KHZ:
		return "93.8";
	case FSK_BW_117_KHZ:
		return "117.3";
	case FSK_BW_156_KHZ:
		return "156.2";
	case FSK_BW_187_KHZ:
		return "187.2";
	case FSK_BW_234_KHZ:
		return "234.3";
	case FSK_BW_312_KHZ:
		return "312.0";
	case FSK_BW_373_KHZ:
		return "373.6";
	case FSK_BW_467_KHZ:
		return "467.0";
	default:
		return "unknown";
	}
}

// Command table
static const shell_cmd_t commands[] = {
	{ "help", cmd_help, "Show available commands", false },
	{ "boot", cmd_boot, "CC1352 bootloader mode", false },
	{ "exit", cmd_exit, "Return to passthrough", false },
	{ "band1", cmd_band1, "2.4GHz band", false },
	{ "band2", cmd_band2, "SUB-GHz band", false },
	{ "band3", cmd_band3, "LoRa band", false },
	{ "reboot", cmd_reboot, "Enter SAMD21 UF2 bootloader", false },
	{ "status", cmd_status, "Device status", false },
	{ "loss_reset", cmd_loss_reset, "Reset CC1352 loss counters", false },
	{ "fw_version", cmd_fw_version, "Show firmware build version", false },
	{ "lora_freq", cmd_lora_freq, "Set frequency (Hz)", true },
	{ "lora_sf", cmd_lora_sf, "Set spreading factor", true },
	{ "lora_bw", cmd_lora_bw, "Set bandwidth (kHz)", true },
	{ "lora_cr", cmd_lora_cr, "Set coding rate", true },
	{ "lora_power", cmd_lora_power, "Set TX power (dBm)", true },
	{ "lora_mode", cmd_lora_mode, "stream|command mode", true },
	{ "lora_preamble", cmd_lora_preamble, "Set preamble length", true },
	{ "lora_syncword", cmd_lora_syncword,
	  "Set sync word (private|public|0xNN)", true },
	{ "lora_iq", cmd_lora_iq, "normal|inverted IQ", true },
	{ "lora_config", cmd_lora_config, "Show LoRa config", false },
	{ "lora_apply", cmd_lora_apply, "Apply pending config", false },
	{ "cc1352_fw_id", cmd_cc1352_fw_id, "set|get|clear|list CC1352 FW ID",
	  true },
	/* FSK/GFSK Commands */
	{ "fsk_freq", cmd_fsk_freq, "Set FSK frequency (Hz)", true },
	{ "fsk_bitrate", cmd_fsk_bitrate, "Set FSK bitrate (bps)", true },
	{ "fsk_fdev", cmd_fsk_fdev, "Set FSK freq deviation (Hz)", true },
	{ "fsk_bw", cmd_fsk_bw, "Set FSK RX bandwidth", true },
	{ "fsk_power", cmd_fsk_power, "Set FSK TX power (dBm)", true },
	{ "fsk_preamble", cmd_fsk_preamble, "Set FSK preamble len", true },
	{ "fsk_syncword", cmd_fsk_syncword, "Set FSK sync word (hex)", true },
	{ "fsk_crc", cmd_fsk_crc, "Enable/disable CRC", true },
	{ "fsk_whitening", cmd_fsk_whitening, "Enable/disable whitening",
	  true },
	{ "fsk_pktlen", cmd_fsk_pktlen, "fixed|variable packet length", true },
	{ "fsk_payload", cmd_fsk_payload, "Set payload/max len (1-255)", true },
	{ "fsk_bt", cmd_fsk_bt, "Set GFSK BT (off|0.3|0.5|0.7|1.0)", true },
	{ "fsk_config", cmd_fsk_config, "Show FSK config", false },
	{ "fsk_apply", cmd_fsk_apply, "Apply FSK config", false },
	{ "modulation", cmd_modulation, "lora|fsk modulation", true },
	{ "radio", cmd_radio, "Forward radio cmd (TEST/FSKRX/FSKTX/TX)", true },
	{ "identify", cmd_identify, "Blink all LEDs to identify this board",
	  false },
	{ NULL, NULL, NULL, false }
};

// Command implementations
static void cmd_help(char *args)
{
	shell_reply("Commands:\r\n");
	for (const shell_cmd_t *cmd = commands; cmd->name != NULL; cmd++) {
		char buf[80];
		snprintf(buf, sizeof(buf), "  %-14s - %s\r\n", cmd->name,
			 cmd->help);
		shell_reply(buf);
	}
	shell_reply("\r\nFSK bandwidths (kHz):\r\n");
	shell_reply("  4.8, 5.8, 7.3, 9.7, 11.7, 14.6, 19.5, 23.4,\r\n");
	shell_reply("  29.3, 39.0, 46.9, 58.6, 78.2, 93.8, 117.3,\r\n");
	shell_reply("  156.2, 187.2, 234.3, 312.0, 373.6, 467.0\r\n");
}

static void cmd_boot(char *args)
{
	change_mode(BOOT);
	set_status_leds(0, 0, catsniffer.mode);
	shell_reply("BOOT\r\n");
}

static void cmd_exit(char *args)
{
	change_mode(PASSTHROUGH);
	set_status_leds(0, 0, 0);
	shell_reply("PASSTHROUGH\r\n");
}

static void cmd_band1(char *args)
{
	change_band(GIG);
	set_status_leds(0, 0, 0);
	shell_reply("2.4GHz Band\r\n");
}

static void cmd_band2(char *args)
{
	change_band(SUBGIG_1);
	set_status_leds(0, 0, 0);
	shell_reply("SUB-GHz Band\r\n");
}

static void cmd_band3(char *args)
{
	change_band(SUBGIG_2);
	set_status_leds(0, 0, 0);
	shell_reply("LoRa Band\r\n");
}

#define UF2_DOUBLE_TAP_MAGIC 0xf01669efUL

static void cmd_reboot(char *args)
{
	uint32_t *sram_top = (uint32_t *)(DT_REG_ADDR(DT_NODELABEL(sram0)) +
					  DT_REG_SIZE(DT_NODELABEL(sram0)));

	shell_reply("Entering UF2 bootloader...\r\n");
	k_msleep(100);
	/* uf2-samdx1 checks this word after reset and stays in bootloader */
	sram_top[-1] = UF2_DOUBLE_TAP_MAGIC;
	sys_reboot(SYS_REBOOT_COLD);
}

/* ISR stack headroom: count the untouched 0xAA fill left by CONFIG_INIT_STACKS
 */
K_KERNEL_STACK_ARRAY_DECLARE(z_interrupt_stacks, CONFIG_MP_MAX_NUM_CPUS,
			     CONFIG_ISR_STACK_SIZE);
static size_t isr_stack_unused(void)
{
	const uint8_t *p =
		(const uint8_t *)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]);
	size_t n = 0;

	while (n < K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]) &&
	       p[n] == 0xAA) {
		n++;
	}
	return n;
}

static void stack_report_cb(const struct k_thread *t, void *user_data)
{
	char *buf = user_data;
	size_t unused = 0;

	k_thread_stack_space_get(t, &unused);
	snprintf(buf, 96, "  thread %p prio=%d stack=%u unused=%u\r\n", t,
		 t->base.prio, (unsigned int)t->stack_info.size,
		 (unsigned int)unused);
	shell_reply(buf);
}

static void cmd_status(char *args)
{
	char buf[256];
	trace_format(buf, sizeof(buf));
	shell_reply(buf);
	const char *mode_str = (catsniffer.lora_mode == LORA_MODE_STREAM) ?
				       "Stream" :
				       "Command";
	const char *lora_status =
		catsniffer.lora_initialized ? "initialized" : "not initialized";
	char fw_id[CC1352_FW_ID_MAX_LEN];
	const char *fw_id_str = "unset";
	const char *fw_type = "n/a";
	if (fw_metadata_get_cc1352_fw_id(fw_id, sizeof(fw_id)) == 0) {
		fw_id_str = fw_id;
		fw_type = fw_metadata_is_official_cc1352_fw_id(fw_id) ? "offici"
									"al" :
									"custo"
									"m";
	}
	snprintf(buf, sizeof(buf),
		 "Mode: %d, Band: %d, Radio: %s, LoRa: %s, LoRa Mode: %s, FW: "
		 "%s, "
		 "CC1352 FW: %s (%s)\r\n",
		 catsniffer.mode, catsniffer.band,
		 catsniffer.current_modulation == LORA_MOD_FSK ? "FSK" : "LoRa",
		 lora_status, mode_str, CATSNIFFER_FW_VERSION, fw_id_str,
		 fw_type);
	shell_reply(buf);

	char loss_buf[96];
	snprintf(loss_buf, sizeof(loss_buf),
		 "CC1352 loss: uart_overrun=%u, ring_dropped=%u bytes, "
		 "dma_regress=%u\r\n",
		 catsniffer.uart_overrun_count, catsniffer.ring_overflow_count,
		 catsniffer.dma_regress_count);
	shell_reply(loss_buf);

	/* SAMD21 only: crash log and stack headroom (16 KB SRAM budget) */
	size_t main_unused = 0, lora_unused = 0;
	k_thread_stack_space_get(k_current_get(), &main_unused);
	k_thread_stack_space_get(&lora_thread, &lora_unused);
	snprintf(loss_buf, sizeof(loss_buf),
		 "Stack unused: main=%u lora=%u isr=%u bytes\r\n",
		 (unsigned int)main_unused, (unsigned int)lora_unused,
		 (unsigned int)isr_stack_unused());
	shell_reply(loss_buf);
	fault_log_format(loss_buf, sizeof(loss_buf));
	shell_reply(loss_buf);
	k_thread_foreach(stack_report_cb, loss_buf);
}

static void cmd_loss_reset(char *args)
{
	catsniffer.uart_overrun_count = 0;
	catsniffer.ring_overflow_count = 0;
	catsniffer.dma_regress_count = 0;
	shell_reply("CC1352 loss counters reset\r\n");
}

static void cmd_fw_version(char *args)
{
	char buf[320];
	snprintf(buf, sizeof(buf),
		 "FW: %s\r\nGit: %s (%s)\r\nBuilt: %s\r\nCompiler: %s %s\r\n"
		 "Board: %s\r\n",
		 CATSNIFFER_FW_VERSION, CATSNIFFER_GIT_SHA,
		 CATSNIFFER_GIT_DIRTY, CATSNIFFER_BUILD_TIME_UTC,
		 CATSNIFFER_COMPILER_ID, CATSNIFFER_COMPILER_VERSION,
		 "v2 SAMD21 CC1352P1");
	shell_reply(buf);
}

#define IDENTIFY_BLINK_STEPS 20 /* 10 on + 10 off = 10 full blinks */
#define IDENTIFY_BLINK_MS 100

static int identify_steps_remaining;

static void identify_timer_expiry(struct k_timer *timer)
{
	if (identify_steps_remaining <= 0) {
		k_timer_stop(timer);
		set_status_leds(0, 0, 0);
		catsniffer.led_identify = false;
		return;
	}
	int on = (identify_steps_remaining % 2 == 0);
	set_status_leds(on, on, on);
	identify_steps_remaining--;
}

K_TIMER_DEFINE(identify_timer, identify_timer_expiry, NULL);

static void cmd_identify(char *args)
{
	k_timer_stop(&identify_timer);
	catsniffer.led_identify = true;
	identify_steps_remaining = IDENTIFY_BLINK_STEPS;
	shell_reply("Identifying board...\r\n");
	k_timer_start(&identify_timer, K_MSEC(IDENTIFY_BLINK_MS),
		      K_MSEC(IDENTIFY_BLINK_MS));
}

static void cmd_cc1352_fw_id(char *args)
{
	char *subcmd;
	char *value;

	while (*args && *args != ' ') {
		args++;
	}
	while (*args == ' ') {
		args++;
	}

	subcmd = args;
	while (*args && *args != ' ') {
		args++;
	}
	if (*args != '\0') {
		*args++ = '\0';
	}
	while (*args == ' ') {
		args++;
	}
	value = args;

	if (subcmd[0] == '\0') {
		shell_reply("Usage: cc1352_fw_id <set|get|clear|list> "
			    "[id]\r\n");
		return;
	}

	if (strcmp(subcmd, "set") == 0) {
		char msg[128];
		const char *type;
		int ret;

		if (value[0] == '\0') {
			shell_reply("Usage: cc1352_fw_id set <id>\r\n");
			return;
		}

		ret = fw_metadata_set_cc1352_fw_id(value);
		if (ret < 0) {
			if (ret == -EINVAL) {
				shell_reply("ERR invalid ID (allowed: a-z A-Z "
					    "0-9 _ - . , max 31)\r\n");
			} else {
				shell_reply("ERR not supported on this "
					    "board\r\n");
			}
			return;
		}

		type = fw_metadata_is_official_cc1352_fw_id(value) ? "officia"
								     "l" :
								     "custom";
		snprintf(msg, sizeof(msg), "OK cc1352_fw_id=%s (%s)\r\n", value,
			 type);
		shell_reply(msg);
		return;
	}

	if (strcmp(subcmd, "get") == 0) {
		char fw_id[CC1352_FW_ID_MAX_LEN];
		char msg[128];
		int ret = fw_metadata_get_cc1352_fw_id(fw_id, sizeof(fw_id));
		if (ret == -ENOENT) {
			shell_reply("OK cc1352_fw_id=unset\r\n");
			return;
		}
		if (ret < 0) {
			shell_reply("ERR not supported on this board\r\n");
			return;
		}

		snprintf(msg, sizeof(msg), "OK cc1352_fw_id=%s type=%s\r\n",
			 fw_id,
			 fw_metadata_is_official_cc1352_fw_id(fw_id) ? "officia"
								       "l" :
								       "custo"
								       "m");
		shell_reply(msg);
		return;
	}

	if (strcmp(subcmd, "clear") == 0) {
		int ret = fw_metadata_clear_cc1352_fw_id();
		if (ret < 0) {
			shell_reply("ERR not supported on this board\r\n");
			return;
		}
		shell_reply("OK cc1352_fw_id cleared\r\n");
		return;
	}

	if (strcmp(subcmd, "list") == 0) {
		char msg[96];
		size_t count = fw_metadata_official_id_count();
		shell_reply("Official CC1352 FW IDs:\r\n");
		for (size_t i = 0; i < count; i++) {
			const char *id = fw_metadata_official_id_by_index(i);
			snprintf(msg, sizeof(msg), "  - %s\r\n", id);
			shell_reply(msg);
		}
		return;
	}

	shell_reply("Usage: cc1352_fw_id <set|get|clear|list> [id]\r\n");
}

static void cmd_lora_freq(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_freq <Hz>\r\n");
		return;
	}

	uint32_t freq = (uint32_t)atoi(args);
	if (freq < 137000000 || freq > 1020000000) {
		shell_reply("Error: Frequency must be 137-1020 MHz\r\n");
		return;
	}

	catsniffer.lora_config.frequency = freq;
	catsniffer.lora_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "Frequency set to %u Hz (pending)\r\n",
		 freq);
	shell_reply(buf);
}

static void cmd_lora_sf(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_sf <7-12>\r\n");
		return;
	}

	int sf = atoi(args);
	if (sf < 7 || sf > 12) {
		shell_reply("Error: Spreading factor must be 7-12\r\n");
		return;
	}

	// Map to Zephyr enum values (SF_7, SF_8, ..., SF_12)
	catsniffer.lora_config.spreading_factor = (uint8_t)sf;
	catsniffer.lora_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "Spreading Factor set to SF%d (pending)\r\n",
		 sf);
	shell_reply(buf);
}

static void cmd_lora_bw(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_bw <125|250|500>\r\n");
		return;
	}

	int bw = atoi(args);
	enum lora_signal_bandwidth bw_enum;

	switch (bw) {
	case 125:
		bw_enum = BW_125_KHZ;
		break;
	case 250:
		bw_enum = BW_250_KHZ;
		break;
	case 500:
		bw_enum = BW_500_KHZ;
		break;
	default:
		shell_reply("Error: Bandwidth must be 125, 250, or 500 "
			    "kHz\r\n");
		return;
	}

	catsniffer.lora_config.bandwidth = bw_enum;
	catsniffer.lora_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "Bandwidth set to %d kHz (pending)\r\n", bw);
	shell_reply(buf);
}

static void cmd_lora_cr(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_cr <5|6|7|8>\r\n");
		return;
	}

	int cr = atoi(args);
	uint8_t cr_enum;

	switch (cr) {
	case 5:
		cr_enum = CR_4_5;
		break;
	case 6:
		cr_enum = CR_4_6;
		break;
	case 7:
		cr_enum = CR_4_7;
		break;
	case 8:
		cr_enum = CR_4_8;
		break;
	default:
		shell_reply("Error: Coding rate must be 5, 6, 7, or 8 (for "
			    "4/5, 4/6, 4/7, 4/8)\r\n");
		return;
	}

	catsniffer.lora_config.coding_rate = cr_enum;
	catsniffer.lora_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "Coding Rate set to 4/%d (pending)\r\n", cr);
	shell_reply(buf);
}

static void cmd_lora_power(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_power <-9 to 22>\r\n");
		return;
	}

	int power = atoi(args);
	if (power < -9 || power > 22) {
		shell_reply("Error: TX power must be -9 to 22 dBm\r\n");
		return;
	}

	catsniffer.lora_config.tx_power = (int8_t)power;
	catsniffer.lora_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "TX Power set to %d dBm (pending)\r\n",
		 power);
	shell_reply(buf);
}

static void cmd_lora_mode(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_mode <stream|command>\r\n");
		return;
	}

	if (strncmp(args, "stream", 6) == 0) {
		catsniffer.lora_mode = LORA_MODE_STREAM;
		catsniffer.led_interval = 1000; // Slow blink for stream mode
		shell_reply("LoRa mode set to STREAM (slow blink)\r\n");
	} else if (strncmp(args, "command", 7) == 0) {
		catsniffer.lora_mode = LORA_MODE_COMMAND;
		catsniffer.led_interval = 200; // Fast blink for command mode
		shell_reply("LoRa mode set to COMMAND (fast blink)\r\n");
	} else {
		shell_reply("Error: Mode must be 'stream' or 'command'\r\n");
	}
}

static void cmd_lora_preamble(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_preamble <6-65535>\r\n");
		return;
	}

	int preamble = atoi(args);
	if (preamble < 6 || preamble > 65535) {
		shell_reply("Error: Preamble length must be 6-65535\r\n");
		return;
	}

	catsniffer.lora_config.preamble_len = (uint16_t)preamble;
	catsniffer.lora_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "Preamble length set to %d (pending)\r\n",
		 preamble);
	shell_reply(buf);
}

static void cmd_lora_syncword(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_syncword <private|public|0xNN>\r\n");
		shell_reply("  private  Private network (reg 0x1424)\r\n");
		shell_reply("  public   Public/LoRaWAN (reg 0x3444)\r\n");
		shell_reply("  0xNN     Custom, e.g. 0x2D for Meshtastic (reg "
			    "0x24D4)\r\n");
		return;
	}

	if (strncmp(args, "private", 7) == 0) {
		catsniffer.lora_config.public_network = false;
		catsniffer.lora_config.lora_sync_word = 0;
		catsniffer.lora_config.config_pending = true;
		shell_reply("Sync word: PRIVATE (reg 0x1424) (pending)\r\n");
	} else if (strncmp(args, "public", 6) == 0) {
		catsniffer.lora_config.public_network = true;
		catsniffer.lora_config.lora_sync_word = 0;
		catsniffer.lora_config.config_pending = true;
		shell_reply("Sync word: PUBLIC/LoRaWAN (reg 0x3444) "
			    "(pending)\r\n");
	} else if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X')) {
		uint8_t sw = (uint8_t)strtoul(args + 2, NULL, 16);
		catsniffer.lora_config.lora_sync_word = sw;
		catsniffer.lora_config.config_pending = true;
		/* Compute register value for display only */
		uint16_t reg = (uint16_t)(((sw & 0xF0U) | 0x04U) << 8) |
			       (((sw & 0x0FU) << 4) | 0x04U);
		char sw_buf[64];
		snprintf(sw_buf, sizeof(sw_buf),
			 "Sync word: 0x%02X (reg 0x%04X) (pending)\r\n", sw,
			 reg);
		shell_reply(sw_buf);
	} else {
		shell_reply("Error: Use 'private', 'public', or '0xNN'\r\n");
	}
}

static void cmd_lora_iq(char *args)
{
	// Skip command name to get argument
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: lora_iq <normal|inverted>\r\n");
		return;
	}

	if (strncmp(args, "normal", 6) == 0) {
		catsniffer.lora_config.iq_inverted = false;
		catsniffer.lora_config.config_pending = true;
		shell_reply("IQ set to NORMAL (pending)\r\n");
	} else if (strncmp(args, "inverted", 8) == 0) {
		catsniffer.lora_config.iq_inverted = true;
		catsniffer.lora_config.config_pending = true;
		shell_reply("IQ set to INVERTED (pending)\r\n");
	} else {
		shell_reply("Error: Must be 'normal' or 'inverted'\r\n");
	}
}

static void cmd_lora_config(char *args)
{
	char buf[512];
	const char *mode_str = (catsniffer.lora_mode == LORA_MODE_STREAM) ?
				       "Stream" :
				       "Command";
	const char *pending_str =
		catsniffer.lora_config.config_pending ? " (pending apply)" : "";
	const char *iq_str = catsniffer.lora_config.iq_inverted ? "Inverted" :
								  "Normal";
	char syncword_str[32];
	uint8_t sw = catsniffer.lora_config.lora_sync_word;
	if (sw != 0) {
		uint16_t reg = (uint16_t)(((sw & 0xF0U) | 0x04U) << 8) |
			       (((sw & 0x0FU) << 4) | 0x04U);
		snprintf(syncword_str, sizeof(syncword_str),
			 "0x%02X (reg 0x%04X)", sw, reg);
	} else if (catsniffer.lora_config.public_network) {
		snprintf(syncword_str, sizeof(syncword_str), "Public (0x34)");
	} else {
		snprintf(syncword_str, sizeof(syncword_str), "Private (0x12)");
	}

	snprintf(buf, sizeof(buf),
		 "LoRa Configuration:%s\r\n"
		 "  Frequency: %u Hz\r\n"
		 "  Spreading Factor: SF%d\r\n"
		 "  Bandwidth: %s kHz\r\n"
		 "  Coding Rate: 4/%d\r\n"
		 "  TX Power: %d dBm\r\n"
		 "  Preamble Length: %d\r\n"
		 "  IQ: %s\r\n"
		 "  Sync Word: %s\r\n"
		 "  Mode: %s\r\n",
		 pending_str, catsniffer.lora_config.frequency,
		 catsniffer.lora_config.spreading_factor,
		 (catsniffer.lora_config.bandwidth == BW_125_KHZ) ? "125" :
		 (catsniffer.lora_config.bandwidth == BW_250_KHZ) ? "250" :
								    "500",
		 (catsniffer.lora_config.coding_rate == CR_4_5) ? 5 :
		 (catsniffer.lora_config.coding_rate == CR_4_6) ? 6 :
		 (catsniffer.lora_config.coding_rate == CR_4_7) ? 7 :
								  8,
		 catsniffer.lora_config.tx_power,
		 catsniffer.lora_config.preamble_len, iq_str, syncword_str,
		 mode_str);
	shell_reply(buf);
}

static void cmd_lora_apply(char *args)
{
	if (!catsniffer.lora_config.config_pending) {
		shell_reply("No pending configuration changes\r\n");
		return;
	}

	int ret = apply_lora_config();
	if (ret < 0) {
		char buf[64];
		snprintf(buf, sizeof(buf),
			 "Error applying configuration: %d\r\n", ret);
		shell_reply(buf);
	} else {
		catsniffer.lora_config.config_pending = false;
		shell_reply("LoRa configuration applied successfully\r\n");
	}
}

/* ============================================ */
/* FSK/GFSK Command Handlers                    */
/* ============================================ */

static void cmd_fsk_freq(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_freq <Hz>\r\n");
		return;
	}

	uint32_t freq = (uint32_t)atoi(args);
	if (freq < 137000000 || freq > 1020000000) {
		shell_reply("Error: Frequency must be 137-1020 MHz\r\n");
		return;
	}

	catsniffer.fsk_config.frequency = freq;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "FSK Frequency set to %u Hz (pending)\r\n",
		 freq);
	shell_reply(buf);
}

static void cmd_fsk_bitrate(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_bitrate <bps>\r\n");
		return;
	}

	uint32_t bitrate = (uint32_t)atoi(args);
	if (bitrate < 600 || bitrate > 300000) {
		shell_reply("Error: Bitrate must be 600-300000 bps\r\n");
		return;
	}

	catsniffer.fsk_config.bitrate = bitrate;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "FSK Bitrate set to %u bps (pending)\r\n",
		 bitrate);
	shell_reply(buf);
}

static void cmd_fsk_fdev(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_fdev <Hz>\r\n");
		return;
	}

	uint32_t fdev = (uint32_t)atoi(args);
	if (fdev < 600 || fdev > 200000) {
		shell_reply("Error: Freq deviation must be 600-200000 Hz\r\n");
		return;
	}

	catsniffer.fsk_config.fdev = fdev;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf),
		 "FSK Freq deviation set to %u Hz (pending)\r\n", fdev);
	shell_reply(buf);
}

static void cmd_fsk_bw(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_bw <bandwidth>\r\n");
		shell_reply("  4.8, 5.8, 7.3, 9.7, 11.7, 14.6, 19.5, "
			    "23.4,\r\n");
		shell_reply("  29.3, 39.0, 46.9, 58.6, 78.2, 93.8, 117.3,\r\n");
		shell_reply("  156.2, 187.2, 234.3, 312.0, 373.6, 467.0 "
			    "kHz\r\n");
		return;
	}

	/* Map the user-supplied kHz value to the nearest lora_fsk_bandwidth
	 * enum entry (public API values are nominal kHz, e.g. FSK_BW_117_KHZ).
	 */
	enum lora_fsk_bandwidth bw_enum;
	float bw_khz = atof(args);

	if (bw_khz <= 5.0f)
		bw_enum = FSK_BW_4_KHZ; /* ~4.8 kHz */
	else if (bw_khz <= 6.0f)
		bw_enum = FSK_BW_5_KHZ; /* ~5.8 kHz */
	else if (bw_khz <= 8.0f)
		bw_enum = FSK_BW_7_KHZ; /* ~7.3 kHz */
	else if (bw_khz <= 10.0f)
		bw_enum = FSK_BW_9_KHZ; /* ~9.7 kHz */
	else if (bw_khz <= 12.0f)
		bw_enum = FSK_BW_11_KHZ; /* ~11.7 kHz */
	else if (bw_khz <= 15.0f)
		bw_enum = FSK_BW_14_KHZ; /* ~14.6 kHz */
	else if (bw_khz <= 20.0f)
		bw_enum = FSK_BW_19_KHZ; /* ~19.5 kHz */
	else if (bw_khz <= 24.0f)
		bw_enum = FSK_BW_23_KHZ; /* ~23.4 kHz */
	else if (bw_khz <= 30.0f)
		bw_enum = FSK_BW_29_KHZ; /* ~29.3 kHz */
	else if (bw_khz <= 40.0f)
		bw_enum = FSK_BW_39_KHZ; /* ~39.0 kHz */
	else if (bw_khz <= 50.0f)
		bw_enum = FSK_BW_46_KHZ; /* ~46.9 kHz */
	else if (bw_khz <= 60.0f)
		bw_enum = FSK_BW_58_KHZ; /* ~58.6 kHz */
	else if (bw_khz <= 80.0f)
		bw_enum = FSK_BW_78_KHZ; /* ~78.2 kHz */
	else if (bw_khz <= 100.0f)
		bw_enum = FSK_BW_93_KHZ; /* ~93.8 kHz */
	else if (bw_khz <= 120.0f)
		bw_enum = FSK_BW_117_KHZ; /* ~117.3 kHz */
	else if (bw_khz <= 160.0f)
		bw_enum = FSK_BW_156_KHZ; /* ~156.2 kHz */
	else if (bw_khz <= 190.0f)
		bw_enum = FSK_BW_187_KHZ; /* ~187.2 kHz */
	else if (bw_khz <= 240.0f)
		bw_enum = FSK_BW_234_KHZ; /* ~234.3 kHz */
	else if (bw_khz <= 320.0f)
		bw_enum = FSK_BW_312_KHZ; /* ~312.0 kHz */
	else if (bw_khz <= 380.0f)
		bw_enum = FSK_BW_373_KHZ; /* ~373.6 kHz */
	else
		bw_enum = FSK_BW_467_KHZ; /* ~467.0 kHz */

	catsniffer.fsk_config.bandwidth = bw_enum;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf),
		 "FSK RX bandwidth set to %s kHz (pending)\r\n",
		 fsk_bw_enum_to_khz_str(bw_enum));
	shell_reply(buf);
}

static void cmd_fsk_power(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_power <-9 to 22>\r\n");
		return;
	}

	int power = atoi(args);
	if (power < -9 || power > 22) {
		shell_reply("Error: TX power must be -9 to 22 dBm\r\n");
		return;
	}

	catsniffer.fsk_config.tx_power = (int8_t)power;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "FSK TX Power set to %d dBm (pending)\r\n",
		 power);
	shell_reply(buf);
}

static void cmd_fsk_preamble(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_preamble <bytes>\r\n");
		return;
	}

	int preamble = atoi(args);
	if (preamble < 0 || preamble > 65535) {
		shell_reply("Error: Preamble must be 0-65535 bytes\r\n");
		return;
	}

	catsniffer.fsk_config.preamble_len = (uint16_t)preamble;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "FSK Preamble set to %d bytes (pending)\r\n",
		 preamble);
	shell_reply(buf);
}

static void cmd_fsk_syncword(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_syncword <hex_bytes>\r\n");
		shell_reply("  Example: fsk_syncword 12AD\r\n");
		return;
	}

	/* Parse hex string to bytes */
	uint8_t sync_word[8] = { 0 };
	uint8_t len = 0;
	char *ptr = args;

	while (*ptr && len < 8) {
		uint8_t byte = 0;
		for (int i = 0; i < 2 && *ptr; i++) {
			byte <<= 4;
			if (*ptr >= '0' && *ptr <= '9')
				byte |= (*ptr - '0');
			else if (*ptr >= 'A' && *ptr <= 'F')
				byte |= (*ptr - 'A' + 10);
			else if (*ptr >= 'a' && *ptr <= 'f')
				byte |= (*ptr - 'a' + 10);
			else
				break;
			ptr++;
		}
		sync_word[len++] = byte;
	}

	if (len == 0) {
		shell_reply("Error: Invalid hex sync word\r\n");
		return;
	}

	memcpy(catsniffer.fsk_config.sync_word, sync_word, len);
	catsniffer.fsk_config.sync_word_len = len;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf),
		 "FSK Sync word set to %d bytes (pending)\r\n", len);
	shell_reply(buf);
}

static void cmd_fsk_crc(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_crc <on|off>\r\n");
		return;
	}

	if (strncmp(args, "on", 2) == 0) {
		catsniffer.fsk_config.crc_on = true;
		catsniffer.fsk_config.config_pending = true;
		shell_reply("FSK CRC enabled (pending)\r\n");
	} else if (strncmp(args, "off", 3) == 0) {
		catsniffer.fsk_config.crc_on = false;
		catsniffer.fsk_config.config_pending = true;
		shell_reply("FSK CRC disabled (pending)\r\n");
	} else {
		shell_reply("Error: Must be 'on' or 'off'\r\n");
	}
}

static void cmd_fsk_whitening(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_whitening <on|off>\r\n");
		return;
	}

	if (strncmp(args, "on", 2) == 0) {
		catsniffer.fsk_config.whitening = true;
		catsniffer.fsk_config.config_pending = true;
		shell_reply("FSK whitening enabled (pending)\r\n");
	} else if (strncmp(args, "off", 3) == 0) {
		catsniffer.fsk_config.whitening = false;
		catsniffer.fsk_config.config_pending = true;
		shell_reply("FSK whitening disabled (pending)\r\n");
	} else {
		shell_reply("Error: Must be 'on' or 'off'\r\n");
	}
}

static void cmd_fsk_pktlen(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_pktlen <fixed|variable>\r\n");
		return;
	}

	if (strncmp(args, "fixed", 5) == 0) {
		catsniffer.fsk_config.fixed_length = true;
		catsniffer.fsk_config.config_pending = true;
		shell_reply("FSK packet mode set to FIXED (pending)\r\n");
	} else if (strncmp(args, "variable", 8) == 0) {
		catsniffer.fsk_config.fixed_length = false;
		catsniffer.fsk_config.config_pending = true;
		shell_reply("FSK packet mode set to VARIABLE (pending)\r\n");
	} else {
		shell_reply("Error: Must be 'fixed' or 'variable'\r\n");
	}
}

static void cmd_fsk_payload(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_payload <1-255>\r\n");
		return;
	}

	int payload = atoi(args);
	if (payload < 1 || payload > 255) {
		shell_reply("Error: Payload length must be 1-255\r\n");
		return;
	}

	catsniffer.fsk_config.payload_len = (uint8_t)payload;
	catsniffer.fsk_config.config_pending = true;

	char buf[64];
	snprintf(buf, sizeof(buf), "FSK payload length set to %d (pending)\r\n",
		 payload);
	shell_reply(buf);
}

static void cmd_fsk_bt(char *args)
{
	enum lora_fsk_shaping shaping;

	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: fsk_bt <off|0.3|0.5|0.7|1.0>\r\n");
		return;
	}

	if (strncmp(args, "off", 3) == 0) {
		shaping = LORA_FSK_SHAPING_NONE;
	} else if (strncmp(args, "0.3", 3) == 0) {
		shaping = LORA_FSK_SHAPING_GAUSS_BT_0_3;
	} else if (strncmp(args, "0.5", 3) == 0) {
		shaping = LORA_FSK_SHAPING_GAUSS_BT_0_5;
	} else if (strncmp(args, "0.7", 3) == 0) {
		shaping = LORA_FSK_SHAPING_GAUSS_BT_0_7;
	} else if (strncmp(args, "1.0", 3) == 0 || strcmp(args, "1") == 0) {
		shaping = LORA_FSK_SHAPING_GAUSS_BT_1_0;
	} else {
		shell_reply("Error: Must be off, 0.3, 0.5, 0.7 or 1.0\r\n");
		return;
	}

	catsniffer.fsk_config.shaping = shaping;
	catsniffer.fsk_config.config_pending = true;
	shell_reply("FSK/GFSK BT shaping updated (pending)\r\n");
}

static void cmd_fsk_config(char *args)
{
	char buf[512];
	const char *pending_str =
		catsniffer.fsk_config.config_pending ? " (pending apply)" : "";
	const char *crc_str = catsniffer.fsk_config.crc_on ? "ON" : "OFF";
	const char *whitening_str = catsniffer.fsk_config.whitening ? "ON" :
								      "OFF";
	const char *pkt_mode_str =
		catsniffer.fsk_config.fixed_length ? "FIXED" : "VARIABLE";
	const char *bw_khz_str =
		fsk_bw_enum_to_khz_str(catsniffer.fsk_config.bandwidth);
	const char *bt_str;

	switch (catsniffer.fsk_config.shaping) {
	case LORA_FSK_SHAPING_NONE:
		bt_str = "OFF";
		break;
	case LORA_FSK_SHAPING_GAUSS_BT_0_3:
		bt_str = "0.3";
		break;
	case LORA_FSK_SHAPING_GAUSS_BT_0_5:
		bt_str = "0.5";
		break;
	case LORA_FSK_SHAPING_GAUSS_BT_0_7:
		bt_str = "0.7";
		break;
	case LORA_FSK_SHAPING_GAUSS_BT_1_0:
		bt_str = "1.0";
		break;
	default:
		bt_str = "unknown";
		break;
	}

	/* Build sync word hex string */
	char sync_str[24] = { 0 };
	for (int i = 0; i < catsniffer.fsk_config.sync_word_len; i++) {
		char byte_str[4];
		snprintf(byte_str, sizeof(byte_str), "%02X",
			 catsniffer.fsk_config.sync_word[i]);
		strcat(sync_str, byte_str);
	}

	snprintf(buf, sizeof(buf),
		 "FSK/GFSK Configuration:%s\r\n"
		 "  Frequency: %u Hz\r\n"
		 "  Bitrate: %u bps\r\n"
		 "  Freq Deviation: %u Hz\r\n"
		 "  RX Bandwidth: %s kHz\r\n"
		 "  TX Power: %d dBm\r\n"
		 "  Gaussian BT: %s\r\n"
		 "  Preamble Length: %u bytes\r\n"
		 "  Sync Word: %s (%d bytes)\r\n"
		 "  Packet Mode: %s\r\n"
		 "  Payload Length: %u\r\n"
		 "  CRC: %s\r\n"
		 "  Whitening: %s\r\n"
		 "  Current Modulation: %s\r\n",
		 pending_str, catsniffer.fsk_config.frequency,
		 catsniffer.fsk_config.bitrate, catsniffer.fsk_config.fdev,
		 bw_khz_str, catsniffer.fsk_config.tx_power, bt_str,
		 catsniffer.fsk_config.preamble_len, sync_str,
		 catsniffer.fsk_config.sync_word_len, pkt_mode_str,
		 catsniffer.fsk_config.payload_len, crc_str, whitening_str,
		 catsniffer.current_modulation == LORA_MOD_FSK ? "FSK" :
								 "LoRa");
	shell_reply(buf);
}

static void cmd_fsk_apply(char *args)
{
	if (!catsniffer.fsk_config.config_pending) {
		shell_reply("No pending FSK configuration changes\r\n");
		return;
	}

	int ret = apply_fsk_config();
	if (ret < 0) {
		char buf[64];
		snprintf(buf, sizeof(buf),
			 "Error applying FSK configuration: %d\r\n", ret);
		shell_reply(buf);
	} else {
		catsniffer.fsk_config.config_pending = false;
		shell_reply("FSK configuration applied successfully\r\n");
	}
}

static void cmd_modulation(char *args)
{
	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		char buf[64];
		shell_reply("Usage: modulation <lora|fsk>\r\n");
		snprintf(buf, sizeof(buf), "  Current: %s\r\n",
			 catsniffer.current_modulation == LORA_MOD_FSK ? "FSK" :
									 "LoR"
									 "a");
		shell_reply(buf);
		return;
	}

	int ret;
	if (strncmp(args, "lora", 4) == 0) {
		ret = switch_to_lora();
		if (ret < 0) {
			shell_reply("Error switching to LoRa\r\n");
		} else {
			shell_reply("Switched to LoRa modulation\r\n");
		}
	} else if (strncmp(args, "fsk", 3) == 0) {
		ret = switch_to_fsk();
		if (ret < 0) {
			shell_reply("Error switching to FSK\r\n");
		} else {
			shell_reply("Switched to FSK modulation\r\n");
		}
	} else {
		shell_reply("Error: Must be 'lora' or 'fsk'\r\n");
	}
}

static void cmd_radio(char *args)
{
	char forwarded[128];
	int ret;

	while (*args && *args != ' ')
		args++;
	while (*args == ' ')
		args++;

	if (*args == '\0') {
		shell_reply("Usage: radio <TEST|FSKRX|FSKTEST|FSKTX HEX|TX "
			    "HEX>\r\n");
		return;
	}

	snprintf(forwarded, sizeof(forwarded), "%s", args);
	ret = queue_radio_command(forwarded);
	if (ret < 0) {
		shell_reply("ERROR: Failed to queue radio command\r\n");
	}
}

// Main command processor
void process_command(char *cmd, size_t len)
{
	if (len == 0)
		return;

	for (const shell_cmd_t *entry = commands; entry->name != NULL;
	     entry++) {
		if (entry->prefix_match) {
			size_t name_len = strlen(entry->name);
			if (strncmp(cmd, entry->name, name_len) == 0) {
				entry->handler(cmd);
				return;
			}
		} else {
			if (strcmp(cmd, entry->name) == 0) {
				entry->handler(cmd);
				return;
			}
		}
	}

	shell_reply("Unknown command. Type 'help'\r\n");
}
