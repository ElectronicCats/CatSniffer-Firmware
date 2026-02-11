#include "fw_metadata.h"

#include <errno.h>
#include <string.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(fw_metadata, LOG_LEVEL_INF);

static const char *const official_fw_ids[] = {
	"sniffle",
	"ti_sniffer",
	"catsniffer_v3",
	"airtag_spoofer_cc1352p7",
	"airtag_scanner_cc1352p7",
};

static struct nvs_fs fs;
static char stored_cc1352_fw_id[CC1352_FW_ID_MAX_LEN];
static int has_fw_id;
static int initialized;
static int storage_unavailable;

#define FW_META_NVS_ID_CC1352_FW_ID 1
#define FW_META_STORAGE_SECTOR_COUNT 2

static int fw_metadata_validate_id(const char *fw_id)
{
	size_t len;

	if (fw_id == NULL) {
		LOG_ERR("Validate: NULL pointer");
		return -EINVAL;
	}

	len = strlen(fw_id);

	// CORREGIDO: Longitud máxima debe ser CC1352_FW_ID_MAX_LEN - 1
	// porque necesitamos espacio para el terminador null
	if (len == 0 || len >= CC1352_FW_ID_MAX_LEN) {
		LOG_ERR("Validate: Invalid length %zu (max %d)", len,
			CC1352_FW_ID_MAX_LEN - 1);
		return -EINVAL;
	}

	for (size_t i = 0; i < len; i++) {
		char c = fw_id[i];

		// CORREGIDO: Alnum ya incluye mayúsculas y minúsculas
		int is_alnum = (c >= '0' && c <= '9') ||
			       (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');

		if (!is_alnum && c != '_' && c != '-' && c != '.') {
			LOG_ERR("Validate: Invalid character '%c' (0x%02x) at "
				"pos %d",
				c, c, i);
			return -EINVAL;
		}
	}

	LOG_DBG("Validate: OK - '%s' (len=%zu)", fw_id, len);
	return 0;
}

int fw_metadata_init(void)
{
	const struct flash_area *fa;
	char tmp[CC1352_FW_ID_MAX_LEN];
	ssize_t len;
	int rc;

	if (initialized) {
		LOG_DBG("Already initialized");
		return 0;
	}

	LOG_DBG("Initializing NVS...");

	rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
	if (rc < 0) {
		LOG_ERR("Failed to open flash area: %d", rc);
		storage_unavailable = 1;
		return rc;
	}

	// MEJORADO: Calcular tamaño de sector correctamente
	uint32_t sector_size = fa->fa_size / FW_META_STORAGE_SECTOR_COUNT;
	// Asegurar que sea múltiplo del tamaño de página
	uint32_t page_size = flash_area_align(fa);
	if (sector_size % page_size != 0) {
		sector_size = (sector_size / page_size) * page_size;
		LOG_WRN("Adjusted sector size to %u (page size %u)",
			sector_size, page_size);
	}

	fs.flash_device = fa->fa_dev;
	fs.offset = fa->fa_off;
	fs.sector_size = sector_size;
	fs.sector_count = FW_META_STORAGE_SECTOR_COUNT;

	LOG_DBG("NVS: dev=%p, offset=0x%x, sector_size=%u, count=%u",
		fs.flash_device, fs.offset, fs.sector_size, fs.sector_count);

	rc = nvs_mount(&fs);
	flash_area_close(fa);

	if (rc < 0) {
		LOG_ERR("Failed to mount NVS: %d", rc);
		storage_unavailable = 1;
		return rc;
	}

	memset(stored_cc1352_fw_id, 0, sizeof(stored_cc1352_fw_id));
	has_fw_id = 0;

	len = nvs_read(&fs, FW_META_NVS_ID_CC1352_FW_ID, tmp, sizeof(tmp) - 1);
	if (len > 0) {
		tmp[len] = '\0';
		LOG_DBG("Read from NVS: '%s' (len=%d)", tmp, len);

		if (fw_metadata_validate_id(tmp) == 0) {
			strncpy(stored_cc1352_fw_id, tmp,
				sizeof(stored_cc1352_fw_id) - 1);
			has_fw_id = 1;
			LOG_INF("Loaded FW ID: %s", stored_cc1352_fw_id);
		} else {
			LOG_WRN("Invalid FW ID in NVS: '%s'", tmp);
		}
	} else if (len < 0) {
		LOG_DBG("No FW ID in NVS (err=%d)", len);
	}

	initialized = 1;
	LOG_DBG("NVS initialized successfully");
	return 0;
}

int fw_metadata_set_cc1352_fw_id(const char *fw_id)
{
	int rc;

	LOG_INF("Setting FW ID to: '%s'", fw_id ? fw_id : "NULL");

	rc = fw_metadata_validate_id(fw_id);
	if (rc < 0) {
		LOG_ERR("Invalid FW ID: %s", fw_id);
		return rc;
	}

	if (!initialized) {
		LOG_DBG("Initializing NVS...");
		rc = fw_metadata_init();
		if (rc < 0) {
			LOG_ERR("Failed to init NVS: %d", rc);
			return rc;
		}
	}

	if (storage_unavailable) {
		LOG_ERR("Storage unavailable");
		return -EIO;
	}

	size_t len = strlen(fw_id);
	LOG_DBG("Writing to NVS: id=%d, data='%s', len=%zu",
		FW_META_NVS_ID_CC1352_FW_ID, fw_id, len);

	rc = nvs_write(&fs, FW_META_NVS_ID_CC1352_FW_ID, fw_id, len);
	if (rc < 0) {
		LOG_ERR("NVS write failed: %d", rc);
		return rc;
	}

	memset(stored_cc1352_fw_id, 0, sizeof(stored_cc1352_fw_id));
	strncpy(stored_cc1352_fw_id, fw_id, sizeof(stored_cc1352_fw_id) - 1);
	has_fw_id = 1;

	LOG_INF("Successfully set FW ID to: %s", stored_cc1352_fw_id);
	return 0;
}

int fw_metadata_get_cc1352_fw_id(char *buf, size_t buf_len)
{
	if (!initialized) {
		int rc = fw_metadata_init();
		if (rc < 0) {
			return rc;
		}
	}

	if (!has_fw_id) {
		LOG_DBG("No FW ID stored");
		return -ENOENT;
	}

	if (buf == NULL || buf_len == 0) {
		LOG_ERR("Invalid buffer");
		return -EINVAL;
	}

	strncpy(buf, stored_cc1352_fw_id, buf_len - 1);
	buf[buf_len - 1] = '\0';

	LOG_DBG("Get FW ID: %s", buf);
	return 0;
}

int fw_metadata_clear_cc1352_fw_id(void)
{
	int rc;

	LOG_INF("Clearing FW ID");

	if (!initialized) {
		rc = fw_metadata_init();
		if (rc < 0) {
			return rc;
		}
	}

	if (storage_unavailable) {
		LOG_ERR("Storage unavailable");
		return -EIO;
	}

	rc = nvs_delete(&fs, FW_META_NVS_ID_CC1352_FW_ID);
	if (rc < 0 && rc != -ENOENT) {
		LOG_ERR("NVS delete failed: %d", rc);
		return rc;
	}

	memset(stored_cc1352_fw_id, 0, sizeof(stored_cc1352_fw_id));
	has_fw_id = 0;

	LOG_INF("FW ID cleared");
	return 0;
}

int fw_metadata_has_cc1352_fw_id(void)
{
	if (!initialized && !storage_unavailable) {
		(void)fw_metadata_init();
	}
	return has_fw_id;
}

int fw_metadata_is_official_cc1352_fw_id(const char *fw_id)
{
	if (!fw_id)
		return 0;

	for (size_t i = 0; i < ARRAY_SIZE(official_fw_ids); i++) {
		if (strcmp(fw_id, official_fw_ids[i]) == 0) {
			LOG_DBG("Official ID: %s", fw_id);
			return 1;
		}
	}
	LOG_DBG("Custom ID: %s", fw_id);
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
