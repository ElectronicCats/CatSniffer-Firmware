#include "cc1352_jtag.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <hardware/gpio.h>
#include <hardware/structs/sio.h>

#define CC1352_JRC_IDCODE_MASK  0x0fffffffu
#define CC1352_JRC_IDCODE_VAL   0x1bb7702fu
#define CC1352_JRC_IDCODE_CORE  0x0bb7702fu
#define ARM_DAP_IDCODE_VAL     0x4ba00477u

#define TAP_CPU_IRLEN 4
#define TAP_JRC_IRLEN 6
#define TAP_MAX 2

#define IR_JRC_IF_BYPASS 0x3f
#define IR_JRC_IDCODE    0x04
#define IR_JRC_ROUTER    0x02
#define IR_JRC_CONNECT   0x07
#define IR_JRC_BYPASS    0x00

#define IR_ARM_DPACC     0x0a
#define IR_ARM_APACC     0x0b
#define IR_ARM_IDCODE    0x0e
#define IR_ARM_BYPASS    0x0f

#define DP_ADDR_ABORT    0x0
#define DP_ADDR_CTRLSTAT 0x4
#define DP_ADDR_SELECT   0x8
#define DP_ADDR_RDBUFF   0xc

#define AP_ADDR_CSW      0x00
#define AP_ADDR_TAR      0x04
#define AP_ADDR_DRW      0x0c

#define CTRLSTAT_CSYSPWRUPREQ BIT(30)
#define CTRLSTAT_CSYSPWRUPACK BIT(31)
#define CTRLSTAT_CDBGPWRUPREQ BIT(28)
#define CTRLSTAT_CDBGPWRUPACK BIT(29)

#define DHCSR_ADDR 0xe000edf0u
#define DHCSR_DBGKEY 0xa05f0000u
#define DHCSR_C_DEBUGEN BIT(0)
#define DHCSR_C_HALT BIT(1)
#define DHCSR_S_HALT BIT(17)
#define JTAG_EDGE_DELAY_US 1

enum jtag_state {
	JTAG_TLR = 0,
	JTAG_RTI,
	JTAG_SD,
	JTAG_CD,
	JTAG_SHD,
	JTAG_E1D,
	JTAG_PD,
	JTAG_E2D,
	JTAG_UD,
	JTAG_SI,
	JTAG_CI,
	JTAG_SHI,
	JTAG_E1I,
	JTAG_PI,
	JTAG_E2I,
	JTAG_UI,
};

struct chain_desc {
	int taps;
	int ir_len[TAP_MAX];
	int cpu_index;
	int jrc_index;
};

struct jtag_ctx {
	struct gpio_dt_spec tck;
	struct gpio_dt_spec tms;
	struct gpio_dt_spec tdi;
	struct gpio_dt_spec tdo;
	struct gpio_dt_spec reset_a;
	struct gpio_dt_spec reset_b;
	uint tck_pin;
	uint tms_pin;
	uint tdi_pin;
	uint tdo_pin;
	uint32_t tck_mask;
	uint32_t tms_mask;
	uint32_t tdi_mask;
	uint32_t tdo_mask;
	uint reset_a_pin;
	uint reset_b_pin;
	uint reset_ctrl_pin;
	uint reset_obs_pin;
	uint8_t reset_ctrl_out_level;
	bool pins_ready;
	enum jtag_state state;

	bool switched_to_4wire;
	bool cpu_tap_enabled;
	bool chain_detected;
	struct chain_desc chain;
	uint32_t jrc_idcode;
	uint32_t cpu_idcode;
	uint32_t last_dhcsr;
	bool data_pins_swapped;
	bool reset_active_low;
	uint8_t reset_source;
};

static struct jtag_ctx g_ctx = {
	.state = JTAG_TLR,
};

static const struct gpio_dt_spec cjtag_tck = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag0), gpios);
static const struct gpio_dt_spec cjtag_tms = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag1), gpios);
static const struct gpio_dt_spec cjtag_tdi = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag2), gpios);
static const struct gpio_dt_spec cjtag_tdo = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag3), gpios);
static const struct gpio_dt_spec cc1352_reset_a = GPIO_DT_SPEC_GET(DT_ALIAS(pin_reset), gpios);
static const struct gpio_dt_spec cc1352_reset_b = GPIO_DT_SPEC_GET(DT_ALIAS(cjtag_reset), gpios);

static int jtag_select_reset_source(uint8_t source)
{
	/* source: 0 -> pin_reset alias, 1 -> cjtag_reset alias */
	if (source == 0) {
		g_ctx.reset_source = 0;
		g_ctx.reset_ctrl_pin = g_ctx.reset_a_pin;
		g_ctx.reset_obs_pin = g_ctx.reset_b_pin;
	} else {
		g_ctx.reset_source = 1;
		g_ctx.reset_ctrl_pin = g_ctx.reset_b_pin;
		g_ctx.reset_obs_pin = g_ctx.reset_a_pin;
	}

	gpio_init(g_ctx.reset_ctrl_pin);
	gpio_set_function(g_ctx.reset_ctrl_pin, GPIO_FUNC_SIO);
	gpio_disable_pulls(g_ctx.reset_ctrl_pin);
	gpio_set_input_enabled(g_ctx.reset_ctrl_pin, false);
	gpio_set_dir(g_ctx.reset_ctrl_pin, GPIO_OUT);
	gpio_put(g_ctx.reset_ctrl_pin, 1);
	g_ctx.reset_ctrl_out_level = 1;

	gpio_init(g_ctx.reset_obs_pin);
	gpio_set_function(g_ctx.reset_obs_pin, GPIO_FUNC_SIO);
	gpio_disable_pulls(g_ctx.reset_obs_pin);
	gpio_set_input_enabled(g_ctx.reset_obs_pin, true);
	gpio_set_dir(g_ctx.reset_obs_pin, GPIO_IN);
	return 0;
}

static int jtag_apply_data_pins(uint tdi_pin, uint tdo_pin)
{
	if (tdi_pin == tdo_pin) {
		return -EINVAL;
	}

	g_ctx.tdi_pin = tdi_pin;
	g_ctx.tdo_pin = tdo_pin;
	g_ctx.tdi_mask = 1u << tdi_pin;
	g_ctx.tdo_mask = 1u << tdo_pin;

	gpio_init(g_ctx.tdi_pin);
	gpio_set_function(g_ctx.tdi_pin, GPIO_FUNC_SIO);
	gpio_pull_up(g_ctx.tdi_pin);
	gpio_set_input_enabled(g_ctx.tdi_pin, false);
	gpio_set_dir(g_ctx.tdi_pin, GPIO_OUT);
	gpio_put(g_ctx.tdi_pin, 1);

	gpio_init(g_ctx.tdo_pin);
	gpio_set_function(g_ctx.tdo_pin, GPIO_FUNC_SIO);
	gpio_disable_pulls(g_ctx.tdo_pin);
	gpio_set_input_enabled(g_ctx.tdo_pin, true);
	gpio_set_dir(g_ctx.tdo_pin, GPIO_IN);

	return 0;
}

static int jtag_config_data_pins(bool swap_data)
{
	uint tdi_pin = swap_data ? (uint)cjtag_tdo.pin : (uint)cjtag_tdi.pin;
	uint tdo_pin = swap_data ? (uint)cjtag_tdi.pin : (uint)cjtag_tdo.pin;
	int ret = jtag_apply_data_pins(tdi_pin, tdo_pin);

	if (ret == 0) {
		g_ctx.data_pins_swapped = swap_data;
	}
	return ret;
}

static int jtag_config_known_data_map(void)
{
	int ret;

	ret = jtag_apply_data_pins((uint)cjtag_tdi.pin, (uint)cjtag_tdo.pin);
	if (ret == 0) {
		g_ctx.data_pins_swapped = false;
	}
	return ret;
}

static const uint8_t state_transitions[16][2] = {
	[JTAG_TLR] = { JTAG_RTI, JTAG_TLR },
	[JTAG_RTI] = { JTAG_RTI, JTAG_SD },
	[JTAG_SD] = { JTAG_CD, JTAG_SI },
	[JTAG_CD] = { JTAG_SHD, JTAG_E1D },
	[JTAG_SHD] = { JTAG_SHD, JTAG_E1D },
	[JTAG_E1D] = { JTAG_PD, JTAG_UD },
	[JTAG_PD] = { JTAG_PD, JTAG_E2D },
	[JTAG_E2D] = { JTAG_SHD, JTAG_UD },
	[JTAG_UD] = { JTAG_RTI, JTAG_SD },
	[JTAG_SI] = { JTAG_CI, JTAG_TLR },
	[JTAG_CI] = { JTAG_SHI, JTAG_E1I },
	[JTAG_SHI] = { JTAG_SHI, JTAG_E1I },
	[JTAG_E1I] = { JTAG_PI, JTAG_UI },
	[JTAG_PI] = { JTAG_PI, JTAG_E2I },
	[JTAG_E2I] = { JTAG_SHI, JTAG_UI },
	[JTAG_UI] = { JTAG_RTI, JTAG_SD },
};

static int jtag_pin_setup(void)
{
	g_ctx.tck = cjtag_tck;
	g_ctx.tms = cjtag_tms;
	g_ctx.reset_a = cc1352_reset_a;
	g_ctx.reset_b = cc1352_reset_b;
	g_ctx.tck_pin = (uint)g_ctx.tck.pin;
	g_ctx.tms_pin = (uint)g_ctx.tms.pin;
	g_ctx.tck_mask = 1u << g_ctx.tck_pin;
	g_ctx.tms_mask = 1u << g_ctx.tms_pin;
	g_ctx.reset_a_pin = (uint)g_ctx.reset_a.pin;
	g_ctx.reset_b_pin = (uint)g_ctx.reset_b.pin;

	if (!device_is_ready(g_ctx.tck.port) || !device_is_ready(g_ctx.tms.port) ||
	    !device_is_ready(cjtag_tdi.port) || !device_is_ready(cjtag_tdo.port) ||
	    !device_is_ready(g_ctx.reset_a.port) ||
	    !device_is_ready(g_ctx.reset_b.port)) {
		return -ENODEV;
	}

	gpio_init(g_ctx.tck_pin);
	gpio_set_function(g_ctx.tck_pin, GPIO_FUNC_SIO);
	gpio_pull_up(g_ctx.tck_pin);
	gpio_set_input_enabled(g_ctx.tck_pin, false);
	gpio_set_dir(g_ctx.tck_pin, GPIO_OUT);
	gpio_put(g_ctx.tck_pin, 1);

	gpio_init(g_ctx.tms_pin);
	gpio_set_function(g_ctx.tms_pin, GPIO_FUNC_SIO);
	gpio_pull_up(g_ctx.tms_pin);
	gpio_set_input_enabled(g_ctx.tms_pin, false);
	gpio_set_dir(g_ctx.tms_pin, GPIO_OUT);
	gpio_put(g_ctx.tms_pin, 1);

	if (jtag_config_data_pins(false) < 0) {
		return -EIO;
	}

	jtag_select_reset_source(0);

	g_ctx.pins_ready = true;
	return 0;
}

static int jtag_apply_ctrl_pins(uint tck_pin, uint tms_pin)
{
	if (tck_pin == tms_pin) {
		return -EINVAL;
	}

	g_ctx.tck_pin = tck_pin;
	g_ctx.tms_pin = tms_pin;
	g_ctx.tck_mask = 1u << g_ctx.tck_pin;
	g_ctx.tms_mask = 1u << g_ctx.tms_pin;

	gpio_init(g_ctx.tck_pin);
	gpio_set_function(g_ctx.tck_pin, GPIO_FUNC_SIO);
	gpio_pull_up(g_ctx.tck_pin);
	gpio_set_input_enabled(g_ctx.tck_pin, false);
	gpio_set_dir(g_ctx.tck_pin, GPIO_OUT);
	gpio_put(g_ctx.tck_pin, 1);

	gpio_init(g_ctx.tms_pin);
	gpio_set_function(g_ctx.tms_pin, GPIO_FUNC_SIO);
	gpio_pull_up(g_ctx.tms_pin);
	gpio_set_input_enabled(g_ctx.tms_pin, false);
	gpio_set_dir(g_ctx.tms_pin, GPIO_OUT);
	gpio_put(g_ctx.tms_pin, 1);

	return 0;
}

static inline void jtag_idle_lines(void)
{
	sio_hw->gpio_set = g_ctx.tck_mask | g_ctx.tms_mask | g_ctx.tdi_mask;
}

static inline void cc1352_set_reset_asserted(bool asserted, bool active_low)
{
	int level = asserted ? (active_low ? 0 : 1) : (active_low ? 1 : 0);

	gpio_put(g_ctx.reset_ctrl_pin, level);
	g_ctx.reset_ctrl_out_level = (uint8_t)level;
}

static void cc1352_reset_pulse_ms(int low_ms, int high_ms, bool active_low)
{
	cc1352_set_reset_asserted(true, active_low);
	k_msleep(low_ms);
	cc1352_set_reset_asserted(false, active_low);
	k_msleep(high_ms);
}

static void cc1352_release_reset_ms(int high_ms, bool active_low)
{
	cc1352_set_reset_asserted(false, active_low);
	k_msleep(high_ms);
}

static void cc1352_force_release_reset(void)
{
	/* Board design and existing firmware use active-low CC1352 reset. */
	gpio_put(g_ctx.reset_ctrl_pin, 1);
}

static uint8_t cc1352_read_reset_ctrl_level(void)
{
	return g_ctx.pins_ready ? g_ctx.reset_ctrl_out_level : 0;
}

static uint8_t cc1352_read_reset_obs_level(void)
{
	return g_ctx.pins_ready ? (gpio_get(g_ctx.reset_obs_pin) ? 1 : 0) : 0;
}

static void cc1352_drive_reset_obs_output(uint8_t level)
{
	if (!g_ctx.pins_ready) {
		return;
	}

	gpio_init(g_ctx.reset_obs_pin);
	gpio_set_function(g_ctx.reset_obs_pin, GPIO_FUNC_SIO);
	gpio_disable_pulls(g_ctx.reset_obs_pin);
	gpio_set_input_enabled(g_ctx.reset_obs_pin, false);
	gpio_set_dir(g_ctx.reset_obs_pin, GPIO_OUT);
	gpio_put(g_ctx.reset_obs_pin, level ? 1 : 0);
}

static void cc1352_restore_reset_obs_input(void)
{
	if (!g_ctx.pins_ready) {
		return;
	}

	gpio_init(g_ctx.reset_obs_pin);
	gpio_set_function(g_ctx.reset_obs_pin, GPIO_FUNC_SIO);
	gpio_disable_pulls(g_ctx.reset_obs_pin);
	gpio_set_input_enabled(g_ctx.reset_obs_pin, true);
	gpio_set_dir(g_ctx.reset_obs_pin, GPIO_IN);
}

static void cc1352_reset_with_line_preset(bool drive_low, int low_ms, int high_ms,
					  bool active_low)
{
	if (drive_low) {
		gpio_put(g_ctx.tms_pin, 0);
		gpio_put(g_ctx.tdi_pin, 0);
		gpio_put(g_ctx.tck_pin, 0);
	} else {
		jtag_idle_lines();
	}

	cc1352_reset_pulse_ms(low_ms, high_ms, active_low);
}

static inline void jtag_clock_edge(int tms, int tdi)
{
	if (tms) {
		sio_hw->gpio_set = g_ctx.tms_mask;
	} else {
		sio_hw->gpio_clr = g_ctx.tms_mask;
	}
	if (tdi) {
		sio_hw->gpio_set = g_ctx.tdi_mask;
	} else {
		sio_hw->gpio_clr = g_ctx.tdi_mask;
	}
	sio_hw->gpio_clr = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
	k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
	sio_hw->gpio_set = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
	k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
	g_ctx.state = state_transitions[g_ctx.state][tms ? 1 : 0];
}

static inline int jtag_clock_edge_read_tdo(int tms, int tdi)
{
	int bit = 0;

	if (tms) {
		sio_hw->gpio_set = g_ctx.tms_mask;
	} else {
		sio_hw->gpio_clr = g_ctx.tms_mask;
	}
	if (tdi) {
		sio_hw->gpio_set = g_ctx.tdi_mask;
	} else {
		sio_hw->gpio_clr = g_ctx.tdi_mask;
	}
	sio_hw->gpio_clr = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
	k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
	bit = (sio_hw->gpio_in & g_ctx.tdo_mask) ? 1 : 0;
	sio_hw->gpio_set = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
	k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
	g_ctx.state = state_transitions[g_ctx.state][tms ? 1 : 0];
	return bit;
}

static void jtag_go_rti(void)
{
	for (int i = 0; i < 6; i++) {
		jtag_clock_edge(1, 1);
	}
	g_ctx.state = JTAG_TLR;
	jtag_clock_edge(0, 1);
	g_ctx.state = JTAG_RTI;
}

static void jtag_force_reset(void)
{
	jtag_idle_lines();
	jtag_go_rti();
}

static void jtag_runtest(int cycles)
{
	if (g_ctx.state != JTAG_RTI) {
		jtag_go_rti();
	}
	for (int i = 0; i < cycles; i++) {
		jtag_clock_edge(0, 0);
	}
}

static inline void fd_jtag_clock_raw(int tms, int tdi)
{
	if (tms) {
		sio_hw->gpio_set = g_ctx.tms_mask;
	} else {
		sio_hw->gpio_clr = g_ctx.tms_mask;
	}
	if (tdi) {
		sio_hw->gpio_set = g_ctx.tdi_mask;
	} else {
		sio_hw->gpio_clr = g_ctx.tdi_mask;
	}
	sio_hw->gpio_clr = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
	k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
	sio_hw->gpio_set = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
	k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
}

static inline void fd_jtag_run_tms_seq(const uint8_t *seq, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		fd_jtag_clock_raw(seq[i] ? 1 : 0, 0);
	}
}

static inline void fd_jtag_goto_rti(void)
{
	for (int i = 0; i < 6; i++) {
		fd_jtag_clock_raw(1, 1);
	}
	fd_jtag_clock_raw(0, 1);
	g_ctx.state = JTAG_RTI;
}

static inline void fd_jtag_runtest(int cycles)
{
	for (int i = 0; i < cycles; i++) {
		fd_jtag_clock_raw(0, 1);
	}
}

static inline uint32_t fd_jtag_read_bits(int size)
{
	uint32_t value = 0;

	for (int i = 0; i < size; i++) {
		sio_hw->gpio_clr = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
		k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
		if (sio_hw->gpio_in & g_ctx.tdo_mask) {
			value |= (1u << i);
		}
		sio_hw->gpio_set = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
		k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
	}

	return value;
}

static inline uint32_t fd_jtag_write_bits(uint32_t value, int size)
{
	for (int i = 0; i < size; i++) {
		if (value & 1u) {
			sio_hw->gpio_set = g_ctx.tdi_mask;
		} else {
			sio_hw->gpio_clr = g_ctx.tdi_mask;
		}
		sio_hw->gpio_clr = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
		k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
		sio_hw->gpio_set = g_ctx.tck_mask;
#if JTAG_EDGE_DELAY_US > 0
		k_busy_wait(JTAG_EDGE_DELAY_US);
#endif
		value >>= 1;
	}

	return value;
}

static void fd_jtag_write_ir_single(uint32_t ir, int ir_len)
{
	/* FreeDAP dap_jtag_write_ir() for a single tap (ir_before=ir_after=0). */
	sio_hw->gpio_set = g_ctx.tms_mask;
	fd_jtag_clock_raw(1, 1); /* Select-DR-Scan */
	fd_jtag_clock_raw(1, 1); /* Select-IR-Scan */
	sio_hw->gpio_clr = g_ctx.tms_mask;
	fd_jtag_clock_raw(0, 1); /* Capture-IR */
	fd_jtag_clock_raw(0, 1); /* Shift-IR */

	ir = fd_jtag_write_bits(ir, ir_len - 1);

	sio_hw->gpio_set = g_ctx.tms_mask;
	(void)fd_jtag_write_bits(ir, 1); /* Exit1-IR */

	fd_jtag_clock_raw(1, 1); /* Update-IR */
	sio_hw->gpio_clr = g_ctx.tms_mask;
	fd_jtag_clock_raw(0, 1); /* Idle */
	sio_hw->gpio_set = g_ctx.tdi_mask;
}

static uint32_t fd_jtag_read_dr32_single(void)
{
	uint32_t data;

	/* FreeDAP dap_jtag_idcode() sequence for single selected tap. */
	sio_hw->gpio_set = g_ctx.tms_mask;
	fd_jtag_clock_raw(1, 1); /* Select-DR-Scan */
	sio_hw->gpio_clr = g_ctx.tms_mask;
	fd_jtag_clock_raw(0, 1); /* Capture-DR */
	fd_jtag_clock_raw(0, 1); /* Shift-DR */

	data = fd_jtag_read_bits(31);
	sio_hw->gpio_set = g_ctx.tms_mask;
	data |= (fd_jtag_read_bits(1) << 31); /* Exit1-DR */

	fd_jtag_clock_raw(1, 1); /* Update-DR */
	sio_hw->gpio_clr = g_ctx.tms_mask;
	fd_jtag_clock_raw(0, 1); /* Idle */
	sio_hw->gpio_set = g_ctx.tdi_mask;

	return data;
}

static uint32_t fd_jtag_read_idcode_after_tlr(void)
{
	fd_jtag_goto_rti();
	fd_jtag_goto_rti();
	return fd_jtag_read_dr32_single();
}

static void jtag_goto_shift_ir(void)
{
	if (g_ctx.state != JTAG_RTI) {
		jtag_go_rti();
	}
	jtag_clock_edge(1, 1);
	jtag_clock_edge(1, 1);
	jtag_clock_edge(0, 1);
	jtag_clock_edge(0, 1);
}

static void jtag_goto_shift_dr(void)
{
	if (g_ctx.state != JTAG_RTI) {
		jtag_go_rti();
	}
	jtag_clock_edge(1, 1);
	jtag_clock_edge(0, 1);
	jtag_clock_edge(0, 1);
}

static bool jtag_pathmove(const enum jtag_state *path, size_t count, int tdi)
{
	if (!path || count < 1) {
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		enum jtag_state next = path[i];
		int tms;

		if (state_transitions[g_ctx.state][0] == next) {
			tms = 0;
		} else if (state_transitions[g_ctx.state][1] == next) {
			tms = 1;
		} else {
			return false;
		}

		jtag_clock_edge(tms, tdi);
	}

	return true;
}

static uint64_t jtag_shift_bits(uint64_t out, int bits)
{
	uint64_t in = 0;

	for (int i = 0; i < bits; i++) {
		int tdi = (out >> i) & 1;
		int tms = (i == bits - 1) ? 1 : 0;
		int tdo = jtag_clock_edge_read_tdo(tms, tdi);
		in |= ((uint64_t)tdo << i);
	}

	jtag_clock_edge(1, 1);
	jtag_clock_edge(0, 1);
	return in;
}

static uint64_t jtag_shift_ir_raw(uint64_t ir, int bits)
{
	jtag_goto_shift_ir();
	return jtag_shift_bits(ir, bits);
}

static uint64_t jtag_shift_ir_from_drpause(uint64_t ir, int bits)
{
	static const enum jtag_state drpause_to_shift_ir[] = {
		JTAG_E2D, JTAG_UD, JTAG_SD, JTAG_SI, JTAG_CI, JTAG_SHI
	};

	if (g_ctx.state != JTAG_PD) {
		jtag_go_rti();
		jtag_goto_shift_ir();
		return jtag_shift_bits(ir, bits);
	}

	if (!jtag_pathmove(drpause_to_shift_ir, ARRAY_SIZE(drpause_to_shift_ir), 0)) {
		jtag_go_rti();
		jtag_goto_shift_ir();
	}
	return jtag_shift_bits(ir, bits);
}

static uint64_t jtag_shift_dr_raw(uint64_t dr, int bits)
{
	jtag_goto_shift_dr();
	return jtag_shift_bits(dr, bits);
}

static uint64_t chain_shift_ir(const struct chain_desc *chain, int target_tap,
			       uint64_t ir_value)
{
	int offset = 0;
	uint64_t out = 0;
	int total = 0;

	for (int i = 0; i < chain->taps; i++) {
		uint64_t bypass = (i == chain->jrc_index) ? IR_JRC_IF_BYPASS :
					       IR_ARM_BYPASS;
		uint64_t v = (i == target_tap) ? ir_value : bypass;
		out |= (v & ((1ull << chain->ir_len[i]) - 1ull)) << offset;
		offset += chain->ir_len[i];
		total += chain->ir_len[i];
	}

	return jtag_shift_ir_raw(out, total);
}

static uint64_t chain_shift_dr(const struct chain_desc *chain, int target_tap,
			       uint64_t dr_value, int dr_bits)
{
	int offset = 0;
	uint64_t out = 0;
	uint64_t in;
	int total = 0;

	for (int i = 0; i < chain->taps; i++) {
		if (i == target_tap) {
			out |= (dr_value & ((1ull << dr_bits) - 1ull)) << offset;
			offset += dr_bits;
			total += dr_bits;
		} else {
			out |= (1ull << offset);
			offset += 1;
			total += 1;
		}
	}

	in = jtag_shift_dr_raw(out, total);

	offset = 0;
	for (int i = 0; i < chain->taps; i++) {
		if (i == target_tap) {
			return (in >> offset) & ((1ull << dr_bits) - 1ull);
		}
		offset += (i == target_tap) ? dr_bits : 1;
	}

	return 0;
}

static uint32_t chain_read_idcode(const struct chain_desc *chain, int tap_index,
				  uint64_t ir_value)
{
	uint64_t in;

	chain_shift_ir(chain, tap_index, ir_value);
	in = chain_shift_dr(chain, tap_index, 0, 32);
	return (uint32_t)in;
}

static bool idcode_is_valid(uint32_t id)
{
	return (id != 0x00000000u) && (id != 0xffffffffu);
}

static bool jrc_id_matches(uint32_t id)
{
	return (id == CC1352_JRC_IDCODE_VAL) ||
	       ((id & CC1352_JRC_IDCODE_MASK) == CC1352_JRC_IDCODE_CORE);
}

static int detect_chain_after_enable(void)
{
	struct chain_desc a = {
		.taps = 2,
		.ir_len = { TAP_CPU_IRLEN, TAP_JRC_IRLEN },
		.cpu_index = 0,
		.jrc_index = 1,
	};
	struct chain_desc b = {
		.taps = 2,
		.ir_len = { TAP_JRC_IRLEN, TAP_CPU_IRLEN },
		.cpu_index = 1,
		.jrc_index = 0,
	};
	uint32_t jrc, cpu;

	jrc = chain_read_idcode(&a, a.jrc_index, IR_JRC_IDCODE);
	cpu = chain_read_idcode(&a, a.cpu_index, IR_ARM_IDCODE);
	if (jrc_id_matches(jrc) &&
	    (cpu == ARM_DAP_IDCODE_VAL || idcode_is_valid(cpu))) {
		g_ctx.chain = a;
		g_ctx.chain_detected = true;
		g_ctx.jrc_idcode = jrc;
		g_ctx.cpu_idcode = cpu;
		return 0;
	}

	jrc = chain_read_idcode(&b, b.jrc_index, IR_JRC_IDCODE);
	cpu = chain_read_idcode(&b, b.cpu_index, IR_ARM_IDCODE);
	if (jrc_id_matches(jrc) &&
	    (cpu == ARM_DAP_IDCODE_VAL || idcode_is_valid(cpu))) {
		g_ctx.chain = b;
		g_ctx.chain_detected = true;
		g_ctx.jrc_idcode = jrc;
		g_ctx.cpu_idcode = cpu;
		return 0;
	}

	return -ENODEV;
}

static uint32_t jrc_read_single_tap_idcode(void)
{
	jtag_go_rti();
	fd_jtag_write_ir_single(IR_JRC_IDCODE, TAP_JRC_IRLEN);
	return fd_jtag_read_dr32_single();
}

static uint32_t try_read_jrc_idcode(void)
{
	struct chain_desc a = {
		.taps = 2,
		.ir_len = { TAP_CPU_IRLEN, TAP_JRC_IRLEN },
		.cpu_index = 0,
		.jrc_index = 1,
	};
	struct chain_desc b = {
		.taps = 2,
		.ir_len = { TAP_JRC_IRLEN, TAP_CPU_IRLEN },
		.cpu_index = 1,
		.jrc_index = 0,
	};
	uint32_t id = 0;

	id = fd_jtag_read_idcode_after_tlr();
	if (jrc_id_matches(id)) {
		return id;
	}

	id = jrc_read_single_tap_idcode();
	if (jrc_id_matches(id)) {
		return id;
	}

	id = chain_read_idcode(&a, a.jrc_index, IR_JRC_IDCODE);
	if (jrc_id_matches(id)) {
		return id;
	}

	id = chain_read_idcode(&b, b.jrc_index, IR_JRC_IDCODE);
	if (jrc_id_matches(id)) {
		return id;
	}

	return 0;
}

static int ti_cjtag_to_4wire_sequence(void)
{
	/* Match OpenOCD ti-cjtag.cfg pathmove sequence exactly. */
	static const enum jtag_state runidle_to_drpause[] = {
		JTAG_SD, JTAG_CD, JTAG_E1D, JTAG_PD
	};
	static const enum jtag_state drpause_to_runidle[] = {
		JTAG_E2D, JTAG_UD, JTAG_RTI
	};
	static const enum jtag_state drpause_to_drshift_to_runidle[] = {
		JTAG_E2D, JTAG_SHD, JTAG_E1D, JTAG_UD, JTAG_RTI
	};
	static const enum jtag_state drpause_to_2bitdrshift_to_runidle[] = {
		JTAG_E2D, JTAG_SHD, JTAG_SHD, JTAG_E1D, JTAG_UD, JTAG_RTI
	};
	static const enum jtag_state drpause_to_2bitdrshift_to_drpause[] = {
		JTAG_E2D, JTAG_SHD, JTAG_SHD, JTAG_E1D, JTAG_PD
	};
	static const enum jtag_state drpause_to_1bitdrshift_to_drpause[] = {
		JTAG_E2D, JTAG_SHD, JTAG_E1D, JTAG_PD
	};

	jtag_go_rti();
	jtag_runtest(20);
	jtag_shift_ir_raw(IR_JRC_IF_BYPASS, TAP_JRC_IRLEN);

	if (!jtag_pathmove(runidle_to_drpause, ARRAY_SIZE(runidle_to_drpause), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(drpause_to_runidle, ARRAY_SIZE(drpause_to_runidle), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(runidle_to_drpause, ARRAY_SIZE(runidle_to_drpause), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(drpause_to_runidle, ARRAY_SIZE(drpause_to_runidle), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(runidle_to_drpause, ARRAY_SIZE(runidle_to_drpause), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(drpause_to_drshift_to_runidle,
			   ARRAY_SIZE(drpause_to_drshift_to_runidle), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(runidle_to_drpause, ARRAY_SIZE(runidle_to_drpause), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(drpause_to_2bitdrshift_to_runidle,
			   ARRAY_SIZE(drpause_to_2bitdrshift_to_runidle), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(runidle_to_drpause, ARRAY_SIZE(runidle_to_drpause), 1)) {
		return -EIO;
	}
	for (int i = 0; i < 4; i++) {
		if (!jtag_pathmove(drpause_to_2bitdrshift_to_drpause,
				   ARRAY_SIZE(drpause_to_2bitdrshift_to_drpause), 1)) {
			return -EIO;
		}
	}
	if (!jtag_pathmove(drpause_to_1bitdrshift_to_drpause,
			   ARRAY_SIZE(drpause_to_1bitdrshift_to_drpause), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(drpause_to_runidle, ARRAY_SIZE(drpause_to_runidle), 1)) {
		return -EIO;
	}
	if (!jtag_pathmove(runidle_to_drpause, ARRAY_SIZE(runidle_to_drpause), 1)) {
		return -EIO;
	}

	jtag_shift_ir_from_drpause(IR_JRC_IF_BYPASS, TAP_JRC_IRLEN);
	jtag_shift_ir_raw(IR_JRC_IDCODE, TAP_JRC_IRLEN);

	g_ctx.switched_to_4wire = true;
	return 0;
}

static uint32_t icepick_router_write(uint8_t block, uint8_t reg, uint32_t payload)
{
	uint32_t val = BIT(31) | ((block & 0x7u) << 28) | ((reg & 0xfu) << 24) |
		       (payload & 0x00ffffffu);

	jtag_shift_ir_raw(IR_JRC_ROUTER, TAP_JRC_IRLEN);
	return (uint32_t)jtag_shift_dr_raw(val, 32);
}

static void icepick_enable_cpu_tap(void)
{
	jtag_shift_ir_raw(IR_JRC_CONNECT, TAP_JRC_IRLEN);
	jtag_shift_dr_raw(0x89, 8);

	icepick_router_write(0x0, 0x1, 0x001000);
	icepick_router_write(0x2, 0x0, 0x110048);
	icepick_router_write(0x2, 0x0, 0x112048);
	icepick_router_write(0x2, 0x0, 0x112148);

	jtag_shift_ir_raw(IR_JRC_BYPASS, TAP_JRC_IRLEN);
	for (int i = 0; i < 10; i++) {
		jtag_clock_edge(0, 1);
	}
	g_ctx.cpu_tap_enabled = true;
}

static int jtag_dpacc_apacc(bool apndp, bool rnw, uint8_t addr, uint32_t *data)
{
	uint8_t req = ((addr & 0xc) >> 2) << 2;
	uint64_t dr_out;
	uint64_t dr_in;
	uint8_t ack;

	req |= apndp ? 1 : 0;
	req |= rnw ? 2 : 0;

	chain_shift_ir(&g_ctx.chain, g_ctx.chain.cpu_index,
		       apndp ? IR_ARM_APACC : IR_ARM_DPACC);

	dr_out = req & 0x7;
	if (!rnw && data) {
		dr_out |= ((uint64_t)(*data) << 3);
	}
	dr_in = chain_shift_dr(&g_ctx.chain, g_ctx.chain.cpu_index, dr_out, 35);
	ack = dr_in & 0x7;
	if (ack != 0x2) {
		return -EIO;
	}

	if (rnw && data) {
		*data = (uint32_t)(dr_in >> 3);
	}

	return 0;
}

static int dp_write(uint8_t addr, uint32_t value)
{
	return jtag_dpacc_apacc(false, false, addr, &value);
}

static int dp_read(uint8_t addr, uint32_t *value)
{
	uint32_t dummy = 0;
	int ret;

	ret = jtag_dpacc_apacc(false, true, addr, &dummy);
	if (ret < 0) {
		return ret;
	}
	return jtag_dpacc_apacc(false, true, DP_ADDR_RDBUFF, value);
}

static int ap_write(uint8_t addr, uint32_t value)
{
	uint32_t select = addr & 0xf0;
	int ret;

	ret = dp_write(DP_ADDR_SELECT, select);
	if (ret < 0) {
		return ret;
	}
	return jtag_dpacc_apacc(true, false, addr, &value);
}

static int ap_read(uint8_t addr, uint32_t *value)
{
	uint32_t select = addr & 0xf0;
	uint32_t dummy = 0;
	int ret;

	ret = dp_write(DP_ADDR_SELECT, select);
	if (ret < 0) {
		return ret;
	}
	ret = jtag_dpacc_apacc(true, true, addr, &dummy);
	if (ret < 0) {
		return ret;
	}
	return jtag_dpacc_apacc(false, true, DP_ADDR_RDBUFF, value);
}

static int memap_write32(uint32_t addr, uint32_t value)
{
	int ret;

	ret = ap_write(AP_ADDR_CSW, 0x23000052);
	if (ret < 0) {
		return ret;
	}
	ret = ap_write(AP_ADDR_TAR, addr);
	if (ret < 0) {
		return ret;
	}
	return ap_write(AP_ADDR_DRW, value);
}

static int memap_read32(uint32_t addr, uint32_t *value)
{
	int ret;

	ret = ap_write(AP_ADDR_CSW, 0x23000052);
	if (ret < 0) {
		return ret;
	}
	ret = ap_write(AP_ADDR_TAR, addr);
	if (ret < 0) {
		return ret;
	}
	return ap_read(AP_ADDR_DRW, value);
}

static int dp_power_up(void)
{
	uint32_t ctrl;
	int ret;

	ret = dp_write(DP_ADDR_ABORT, 0x1e);
	if (ret < 0) {
		return ret;
	}

	ret = dp_write(DP_ADDR_CTRLSTAT, CTRLSTAT_CSYSPWRUPREQ | CTRLSTAT_CDBGPWRUPREQ);
	if (ret < 0) {
		return ret;
	}

	for (int i = 0; i < 20; i++) {
		ret = dp_read(DP_ADDR_CTRLSTAT, &ctrl);
		if (ret < 0) {
			return ret;
		}
		if ((ctrl & (CTRLSTAT_CSYSPWRUPACK | CTRLSTAT_CDBGPWRUPACK)) ==
		    (CTRLSTAT_CSYSPWRUPACK | CTRLSTAT_CDBGPWRUPACK)) {
			return 0;
		}
		k_msleep(5);
	}

	return -ETIMEDOUT;
}

int cc1352_jtag_init_sequence(void)
{
	int ret;
	uint32_t jrc_id = 0;
	bool found_jrc = false;
	static const uint8_t data_pairs[][2] = {
		{ 13, 14 },
		{ 14, 13 },
	};
	static const uint8_t reset_sources[] = { 0, 1 };
	static const uint8_t aux_levels[] = { 0, 1 };

	if (!g_ctx.pins_ready) {
		ret = jtag_pin_setup();
		if (ret < 0) {
			return ret;
		}
	}

	/* Fixed baseline state */
	g_ctx.switched_to_4wire = false;
	g_ctx.cpu_tap_enabled = false;
	g_ctx.chain_detected = false;
	g_ctx.chain.taps = 0;
	g_ctx.chain.cpu_index = 0;
	g_ctx.chain.jrc_index = 0;
	g_ctx.jrc_idcode = 0;
	g_ctx.cpu_idcode = 0;
	g_ctx.last_dhcsr = 0;
	g_ctx.reset_active_low = true;
	ret = jtag_apply_ctrl_pins((uint)cjtag_tck.pin, (uint)cjtag_tms.pin);
	if (ret < 0) {
		return ret;
	}

	ret = jtag_config_known_data_map();
	if (ret < 0) {
		return ret;
	}
	g_ctx.data_pins_swapped = false;

	/*
	 * Match free-dap RP2040 mapping:
	 * TCK=11, TMS=12, TDI=13, TDO=14, nRESET=15.
	 */
	/* Stage 1: detect JRC using plain 4-wire first, TI sequence as fallback. */
	for (size_t rs = 0; rs < ARRAY_SIZE(reset_sources) && !found_jrc; rs++) {
		for (size_t al = 0; al < ARRAY_SIZE(aux_levels) && !found_jrc; al++) {
			ret = jtag_select_reset_source(reset_sources[rs]);
			if (ret < 0) {
				continue;
			}
			g_ctx.reset_active_low = true;
			cc1352_force_release_reset();
			cc1352_drive_reset_obs_output(aux_levels[al]);
			k_msleep(20);
			jtag_idle_lines();

			for (size_t dp = 0; dp < ARRAY_SIZE(data_pairs) && !found_jrc; dp++) {
				ret = jtag_apply_data_pins(data_pairs[dp][0], data_pairs[dp][1]);
				if (ret < 0) {
					continue;
				}
				g_ctx.data_pins_swapped = (data_pairs[dp][0] != 13);

				for (int pass = 0; pass < 4 && !found_jrc; pass++) {
					{
						unsigned int key = irq_lock();
						g_ctx.state = JTAG_TLR;
						jrc_id = try_read_jrc_idcode();
						ret = (jrc_id != 0) ? 0 : -ENODEV;
						if (ret < 0) {
							ret = ti_cjtag_to_4wire_sequence();
							if (ret == 0) {
								jrc_id = try_read_jrc_idcode();
								ret = (jrc_id != 0) ? 0 : -ENODEV;
							}
						}
						irq_unlock(key);
					}

					if (ret == 0 && jrc_id_matches(jrc_id)) {
						found_jrc = true;
						g_ctx.jrc_idcode = jrc_id;
						break;
					}

					cc1352_reset_pulse_ms(80, 80, true);
					jtag_idle_lines();
				}
			}
		}
	}

	if (!found_jrc) {
		jtag_select_reset_source(0);
		cc1352_restore_reset_obs_input();
		jtag_apply_ctrl_pins((uint)cjtag_tck.pin, (uint)cjtag_tms.pin);
		jtag_config_data_pins(false);
		g_ctx.reset_active_low = true;
		cc1352_force_release_reset();
		return -ENODEV;
	}

	g_ctx.switched_to_4wire = true;

	/* Stage 2: with verified JRC path, enable CPU TAP and detect chain. */
	{
		unsigned int key = irq_lock();
		icepick_enable_cpu_tap();
		ret = detect_chain_after_enable();
		irq_unlock(key);
	}

	jtag_select_reset_source(0);
	cc1352_restore_reset_obs_input();
	jtag_apply_ctrl_pins((uint)cjtag_tck.pin, (uint)cjtag_tms.pin);
	jtag_config_data_pins(false);
	g_ctx.reset_active_low = true;
	cc1352_force_release_reset();
	return ret;
}

int cc1352_jtag_read_idcodes(uint32_t *jrc_id, uint32_t *cpu_id)
{
	int ret;

	if (!g_ctx.chain_detected) {
		ret = cc1352_jtag_init_sequence();
		if (ret < 0) {
			return ret;
		}
	}

	g_ctx.jrc_idcode = chain_read_idcode(&g_ctx.chain, g_ctx.chain.jrc_index,
					     IR_JRC_IDCODE);
	g_ctx.cpu_idcode = chain_read_idcode(&g_ctx.chain, g_ctx.chain.cpu_index,
					     IR_ARM_IDCODE);

	if (jrc_id) {
		*jrc_id = g_ctx.jrc_idcode;
	}
	if (cpu_id) {
		*cpu_id = g_ctx.cpu_idcode;
	}

	return 0;
}

int cc1352_jtag_halt_cpu(uint32_t *dhcsr_out)
{
	uint32_t dhcsr = 0;
	int ret;

	if (!g_ctx.chain_detected) {
		ret = cc1352_jtag_init_sequence();
		if (ret < 0) {
			return ret;
		}
	}

	ret = dp_power_up();
	if (ret < 0) {
		return ret;
	}

	ret = memap_write32(DHCSR_ADDR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT);
	if (ret < 0) {
		return ret;
	}

	ret = memap_read32(DHCSR_ADDR, &dhcsr);
	if (ret < 0) {
		return ret;
	}

	g_ctx.last_dhcsr = dhcsr;
	if (dhcsr_out) {
		*dhcsr_out = dhcsr;
	}

	if ((dhcsr & DHCSR_S_HALT) == 0) {
		return -EAGAIN;
	}

	return 0;
}

void cc1352_jtag_get_status(struct cc1352_jtag_status *status)
{
	if (!status) {
		return;
	}

	status->pins_ready = g_ctx.pins_ready;
	status->switched_to_4wire = g_ctx.switched_to_4wire;
	status->cpu_tap_enabled = g_ctx.cpu_tap_enabled;
	status->chain_detected = g_ctx.chain_detected;
	status->chain_taps = g_ctx.chain.taps;
	status->cpu_tap_index = g_ctx.chain.cpu_index;
	status->jrc_tap_index = g_ctx.chain.jrc_index;
	status->jrc_idcode = g_ctx.jrc_idcode;
	status->cpu_idcode = g_ctx.cpu_idcode;
	status->last_dhcsr = g_ctx.last_dhcsr;
	status->data_pins_swapped = g_ctx.data_pins_swapped;
	status->reset_active_low = g_ctx.reset_active_low;
	status->reset_ctrl_level = cc1352_read_reset_ctrl_level();
	status->reset_obs_level = cc1352_read_reset_obs_level();
	status->reset_source = g_ctx.reset_source;
	status->tck_pin = (uint8_t)g_ctx.tck_pin;
	status->tms_pin = (uint8_t)g_ctx.tms_pin;
	status->tdi_pin = (uint8_t)g_ctx.tdi_pin;
	status->tdo_pin = (uint8_t)g_ctx.tdo_pin;
}

int cc1352_jtag_diag_probe(struct cc1352_jtag_diag *diag)
{
	int ret;

	if (!diag) {
		return -EINVAL;
	}

	if (!g_ctx.pins_ready) {
		ret = jtag_pin_setup();
		if (ret < 0) {
			return ret;
		}
	}

	for (int map = 0; map < 2; map++) {
		jtag_select_reset_source(1);
		g_ctx.reset_active_low = true;
		ret = jtag_config_data_pins(map == 1);
		if (ret < 0) {
			return ret;
		}

		/* Check if selected TDO input is actually driven by target. */
		gpio_set_pulls(g_ctx.tdo_pin, true, false);
		k_msleep(1);
		diag->tdo_pull_up_level[map] = gpio_get(g_ctx.tdo_pin) ? 1 : 0;

		gpio_set_pulls(g_ctx.tdo_pin, false, true);
		k_msleep(1);
		diag->tdo_pull_down_level[map] =
			gpio_get(g_ctx.tdo_pin) ? 1 : 0;

		gpio_disable_pulls(g_ctx.tdo_pin);

		cc1352_release_reset_ms(20, true);
		k_msleep(5);
		jtag_idle_lines();
		{
			unsigned int key = irq_lock();
			g_ctx.state = JTAG_TLR;
			diag->direct_jrc_id[map] = fd_jtag_read_idcode_after_tlr();
			irq_unlock(key);
		}

		cc1352_release_reset_ms(20, true);
		k_msleep(5);
		jtag_idle_lines();
		{
			unsigned int key = irq_lock();
			g_ctx.state = JTAG_RTI;
			ret = ti_cjtag_to_4wire_sequence();
			if (ret == 0) {
				ret = ti_cjtag_to_4wire_sequence();
			}
			diag->switched_jrc_id[map] =
				(ret == 0) ? jrc_read_single_tap_idcode() : 0;
			irq_unlock(key);
		}
	}

	jtag_select_reset_source(1);
	g_ctx.reset_active_low = true;
	jtag_config_data_pins(false);
	cc1352_force_release_reset();
	return 0;
}

const char *cc1352_jtag_strerror(int err)
{
	switch (-err) {
	case 0:
		return "OK";
	case ENODEV:
		return "JTAG target not found";
	case EIO:
		return "JTAG-DP/AP transfer failed";
	case ETIMEDOUT:
		return "DAP power-up timeout";
	case EAGAIN:
		return "CPU did not enter halt state";
	default:
		return "JTAG operation failed";
	}
}
