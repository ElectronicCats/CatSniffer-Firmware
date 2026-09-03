/*
 * fault_log.c - minimal crash log for boards without a debug console
 *
 * On a fatal error the reason and faulting PC/LR are stored in a no-init
 * SRAM block and the board resets. After reboot "status" prints the entry.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/linker/sections.h>
#include <stdio.h>
#include "catsniffer.h"

#define FAULT_LOG_MAGIC 0xFA17106DUL
#define TRACE_N 24
#define TRACE_LOG_MAGIC 0x7ACE106DUL

static struct {
	uint32_t magic;
	uint32_t reason;
	uint32_t pc;
	uint32_t lr;
	uint32_t count;
} fault_log __noinit;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	if (fault_log.magic != FAULT_LOG_MAGIC) {
		fault_log.magic = FAULT_LOG_MAGIC;
		fault_log.count = 0;
	}
	fault_log.reason = reason;
	fault_log.pc = esf ? esf->basic.pc : 0;
	fault_log.lr = esf ? esf->basic.lr : 0;
	fault_log.count++;
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}

/* Event trace: last TRACE_N stage codes, survives reset if the bootloader
 * leaves this RAM alone. Read with "status".
 */
static struct {
	uint32_t magic;
	uint32_t count;
	uint8_t codes[TRACE_N];
} trace_log __noinit;

void trace_event(uint8_t code)
{
	unsigned int key = irq_lock();

	if (trace_log.magic != TRACE_LOG_MAGIC) {
		trace_log.magic = TRACE_LOG_MAGIC;
		trace_log.count = 0;
	}
	trace_log.codes[trace_log.count % TRACE_N] = code;
	trace_log.count++;
	irq_unlock(key);
}

int trace_format(char *buf, size_t len)
{
	int n;

	if (trace_log.magic != TRACE_LOG_MAGIC) {
		return snprintf(buf, len, "Trace: none\r\n");
	}
	n = snprintf(buf, len, "Trace(%u):", (unsigned int)trace_log.count);
	for (uint32_t i = 0; i < TRACE_N && i < trace_log.count; i++) {
		uint32_t idx = (trace_log.count -
				MIN(trace_log.count, (uint32_t)TRACE_N) + i) %
			       TRACE_N;
		n += snprintf(buf + n, len - n, " %02x", trace_log.codes[idx]);
	}
	n += snprintf(buf + n, len - n, "\r\n");
	return n;
}

int fault_log_format(char *buf, size_t len)
{
	/* Reason codes are small; anything else is stale RAM from another build
	 */
	if (fault_log.magic != FAULT_LOG_MAGIC || fault_log.count == 0 ||
	    fault_log.reason > 64 || fault_log.count > 100000) {
		return snprintf(buf, len, "Last fault: none\r\n");
	}
	return snprintf(buf, len,
			"Last fault: reason=%u pc=0x%08x lr=0x%08x "
			"count=%u\r\n",
			(unsigned int)fault_log.reason,
			(unsigned int)fault_log.pc, (unsigned int)fault_log.lr,
			(unsigned int)fault_log.count);
}
