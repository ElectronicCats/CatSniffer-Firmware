/*
 * fw_metadata.c - stub for SAMD21 (no NVS storage)
 * CC1352 firmware ID is not persisted on v1/v2 hardware.
 */

#include "fw_metadata.h"
#include <errno.h>
#include <string.h>
#include <zephyr/sys/util.h>

static const char *const official_fw_ids[] = {
	"sniffle",
	"ti_sniffer",
	"catsniffer_v3",
	"airtag_spoofer_cc1352p7",
	"airtag_scanner_cc1352p7",
};

int fw_metadata_init(void)
{
	return 0;
}

int fw_metadata_set_cc1352_fw_id(const char *fw_id)
{
	ARG_UNUSED(fw_id);
	return -ENOTSUP;
}

int fw_metadata_get_cc1352_fw_id(char *buf, size_t buf_len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(buf_len);
	return -ENOENT;
}

int fw_metadata_clear_cc1352_fw_id(void)
{
	return -ENOTSUP;
}

int fw_metadata_has_cc1352_fw_id(void)
{
	return 0;
}

int fw_metadata_is_official_cc1352_fw_id(const char *fw_id)
{
	for (size_t i = 0; i < ARRAY_SIZE(official_fw_ids); i++) {
		if (strcmp(fw_id, official_fw_ids[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

const char *fw_metadata_official_id_by_index(size_t index)
{
	if (index >= ARRAY_SIZE(official_fw_ids)) {
		return NULL;
	}
	return official_fw_ids[index];
}

size_t fw_metadata_official_id_count(void)
{
	return ARRAY_SIZE(official_fw_ids);
}
