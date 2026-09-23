/******************************************************************************

 @file  ble_spam_models.h

 @brief Portable spam model table: modes, payload types and the API used
	by the advertising engine (Phase 3). This header must stay free of
	any TI or ESP header so the implementation can be unit-tested on
	the host.

 Group: WCS, BTS
 Target Device: cc13xx_cc26xx

 *****************************************************************************/

#ifndef BLE_SPAM_MODELS_H
#define BLE_SPAM_MODELS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * CONSTANTS
 */

/*
 * Spam modes. Mirrors bt_spam_mode_t of the reference implementation
 * (Minino firmware/components/bt_spam/bt_spam.h).
 */
typedef enum {
	BT_SPAM_MODE_ALL = 0,
	BT_SPAM_MODE_APPLE,
	BT_SPAM_MODE_ANDROID,
	BT_SPAM_MODE_WINDOWS,
	BT_SPAM_MODE_SAMSUNG,
	BT_SPAM_MODE_MAX,
} bt_spam_mode_t;

/*
 * Payload families. Mirrors spam_packet_type_t of the reference
 * implementation.
 */
typedef enum {
	SPAM_TYPE_APPLE_PROXIMITY = 0, /* Continuity type 0x07  */
	SPAM_TYPE_APPLE_ACTION,	       /* Continuity type 0x10  */
	SPAM_TYPE_GOOGLE_FAST_PAIR,    /* Fast Pair 0xFE2C      */
	SPAM_TYPE_MICROSOFT_SWIFT,     /* Swift Pair            */
	SPAM_TYPE_SAMSUNG_SETUP,       /* Company ID 0x0075     */
} spam_packet_type_t;

/*********************************************************************
 * MACROS
 */

/*
 * Maximum advertising payload length (BLE legacy ADV).
 */
#define SPAM_MODEL_MAX_LEN 31

/*********************************************************************
 * TYPEDEFS
 */

/*
 * One advertisable model. `type` holds a spam_packet_type_t value.
 * The data bytes are the advertising payload of the model, copied
 * verbatim from the reference implementation.
 */
typedef struct {
	const char *name;
	uint8_t type; /* spam_packet_type_t */
	uint8_t len;
	uint8_t data[SPAM_MODEL_MAX_LEN];
} spam_model_t;

/*********************************************************************
 * API
 */

/*
 * Initialize the module: default seed and the full model list active
 * (BT_SPAM_MODE_ALL). Safe to call more than once.
 */
extern void spam_models_init(void);

/*
 * Seed the local PRNG (xorshift32). Pass the value returned by the
 * TRNG driver on the target, or any fixed value in host tests.
 * A seed of 0 is remapped to a non-zero constant (xorshift32 would
 * otherwise get stuck).
 */
extern void spam_models_seed(uint32_t seed);

/*
 * Rebuild the active model list for the given mode. Semantics match
 * the reference: APPLE = Proximity + Action, ANDROID = Fast Pair,
 * WINDOWS = Swift Pair, SAMSUNG = 0x0075, ALL = every model. An empty
 * result falls back to the full list.
 */
extern void spam_models_rebuild_active(bt_spam_mode_t mode);

/*
 * Number of models in the active list (after spam_models_rebuild_active).
 */
extern uint8_t spam_models_count(void);

/*
 * Translate a position of the active list into its index in the global
 * spam_models[] table. Out-of-range positions wrap around.
 */
extern uint8_t spam_models_index(uint8_t active_idx);

/*
 * Copy the model at position `active_idx` of the active list into `out`,
 * randomizing the ephemeral bytes of the payload for the model type to
 * bypass receiver deduplication filters (same bytes as the reference
 * implementation). Returns the payload length, or 0 on error (`out`
 * NULL or empty active list).
 */
extern uint8_t spam_models_prepare(uint8_t active_idx, spam_model_t *out);

/*
 * Short printable name of a mode ("ALL", "APPLE", ...). Never NULL.
 */
extern const char *spam_models_mode_name(bt_spam_mode_t mode);

/*********************************************************************
 *********************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* BLE_SPAM_MODELS_H */
