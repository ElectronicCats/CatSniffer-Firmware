/*
 * clock_usbcrm.c - DFLL48M in USB clock recovery mode for crystal-less boards
 *
 * CatSniffer v1/v2 has no external crystal. Zephyr's SAMD2x SoC init locks
 * the DFLL48M to the internal OSC8M (about 1% accurate), which is outside the
 * 0.25% USB full-speed tolerance: short control transfers succeed but the
 * configuration descriptor fails and the host never configures the device.
 *
 * This early init re-locks the DFLL48M to the USB 1 kHz start-of-frame
 * (USBCRM), the same sequence the Electronic Cats Arduino core uses in
 * startup.c for CRYSTALLESS boards. It runs after soc_reset_hook(), so GCLK0
 * is temporarily moved back to OSC8M while the DFLL is reconfigured.
 */

#include <zephyr/init.h>
#include <soc.h>

#define DFLL48M_USB_MUL 0xBB80 /* 48 MHz / 1 kHz SOF */
#define DFLL48M_USB_FSTEP 0xA  /* datasheet, USB characteristics */
#define DFLL48M_USB_CSTEP 31   /* half of maximum */

static void gclk0_source(uint32_t src)
{
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(0) | GCLK_GENDIV_DIV(0);
	while (GCLK->STATUS.bit.SYNCBUSY) {
	}
	GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(0) | GCLK_GENCTRL_GENEN | src;
	while (GCLK->STATUS.bit.SYNCBUSY) {
	}
}

static int clock_usbcrm_init(void)
{
	uint32_t coarse;

	/* OSC8M is left enabled by the SoC init (CONFIG_SOC_ATMEL_SAMD_OSC8M)
	 */
	gclk0_source(GCLK_GENCTRL_SRC_OSC8M);

	/* Errata: DFLLCTRL writes require ONDEMAND=0 and DFLLRDY */
	SYSCTRL->DFLLCTRL.reg = 0;
	while (!SYSCTRL->PCLKSR.bit.DFLLRDY) {
	}

	coarse = (*((uint32_t *)FUSES_DFLL48M_COARSE_CAL_ADDR) &
		  FUSES_DFLL48M_COARSE_CAL_Msk) >>
		 FUSES_DFLL48M_COARSE_CAL_Pos;
	SYSCTRL->DFLLVAL.reg = SYSCTRL_DFLLVAL_COARSE(coarse) |
			       SYSCTRL_DFLLVAL_FINE(512);

	SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_CSTEP(DFLL48M_USB_CSTEP) |
			       SYSCTRL_DFLLMUL_FSTEP(DFLL48M_USB_FSTEP) |
			       SYSCTRL_DFLLMUL_MUL(DFLL48M_USB_MUL);

	SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_USBCRM |
				SYSCTRL_DFLLCTRL_MODE | SYSCTRL_DFLLCTRL_CCDIS;
	while (!SYSCTRL->PCLKSR.bit.DFLLRDY) {
	}

	SYSCTRL->DFLLCTRL.reg |= SYSCTRL_DFLLCTRL_ENABLE;
	while (!SYSCTRL->PCLKSR.bit.DFLLRDY) {
	}

	gclk0_source(GCLK_GENCTRL_SRC_DFLL48M);

	return 0;
}

SYS_INIT(clock_usbcrm_init, PRE_KERNEL_1, 0);
