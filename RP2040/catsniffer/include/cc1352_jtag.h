#ifndef CC1352_JTAG_H
#define CC1352_JTAG_H

#include <stdbool.h>
#include <stdint.h>

struct cc1352_jtag_status {
	bool pins_ready;
	bool switched_to_4wire;
	bool cpu_tap_enabled;
	bool chain_detected;
	int chain_taps;
	int cpu_tap_index;
	int jrc_tap_index;
	uint32_t jrc_idcode;
	uint32_t cpu_idcode;
	uint32_t last_dhcsr;
	bool data_pins_swapped;
	bool reset_active_low;
	uint8_t reset_ctrl_level;
	uint8_t reset_obs_level;
	uint8_t reset_source;
	uint8_t tck_pin;
	uint8_t tms_pin;
	uint8_t tdi_pin;
	uint8_t tdo_pin;
};

struct cc1352_jtag_diag {
	uint32_t direct_jrc_id[2];
	uint32_t switched_jrc_id[2];
	uint8_t tdo_pull_up_level[2];
	uint8_t tdo_pull_down_level[2];
};

int cc1352_jtag_init_sequence(void);
int cc1352_jtag_read_idcodes(uint32_t *jrc_id, uint32_t *cpu_id);
int cc1352_jtag_halt_cpu(uint32_t *dhcsr_out);
void cc1352_jtag_get_status(struct cc1352_jtag_status *status);
int cc1352_jtag_diag_probe(struct cc1352_jtag_diag *diag);
const char *cc1352_jtag_strerror(int err);

#endif /* CC1352_JTAG_H */
