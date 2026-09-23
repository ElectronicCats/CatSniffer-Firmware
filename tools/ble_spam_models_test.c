/******************************************************************************

 @file  ble_spam_models_test.c

 @brief Host-side unit test for the portable spam model layer
	(CC1352P7/ble_spam_CC1352P_7/Application/ble_spam_models.c).

	Validates the Phase 2 success criteria of docs/plan-ble-spam.md:
	(a) 82 models, (b) len <= 31 and AD-structure coherence,
	(c) per-mode counts, (d) payload randomization with a fixed seed.

	Build (from the repository root, no TI toolchain required):

	gcc -Wall -Wextra -o /tmp/spam_test tools/ble_spam_models_test.c \
	    CC1352P7/ble_spam_CC1352P_7/Application/ble_spam_models.c

 *****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "../CC1352P7/ble_spam_CC1352P_7/Application/ble_spam_models.h"

/* Expected model counts (see Phase 5 of docs/plan-ble-spam.md). */
#define EXPECTED_TOTAL 82
#define EXPECTED_APPLE 22   /* 14 Proximity 0x07 + 8 Action 0x10 */
#define EXPECTED_ANDROID 10 /* Fast Pair 0xFE2C                    */
#define EXPECTED_WINDOWS 4  /* Swift Pair                           */
#define EXPECTED_SAMSUNG 46 /* 26 Watch + 20 Buds (0x0075)          */

static int failures = 0;

#define CHECK(cond, ...)                                             \
	do {                                                         \
		if (!(cond)) {                                       \
			failures++;                                  \
			printf("FAIL(%s:%d): ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);                         \
			printf("\n");                                \
		}                                                    \
	} while (0)

/* (a) + (c): total and per-mode model counts. */
static void test_counts(void)
{
	int total;

	spam_models_rebuild_active(BT_SPAM_MODE_ALL);
	total = spam_models_count();
	printf("[counts] ALL     = %d\n", total);
	CHECK(total == EXPECTED_TOTAL, "ALL: expected %d, got %d",
	      EXPECTED_TOTAL, total);

	spam_models_rebuild_active(BT_SPAM_MODE_APPLE);
	printf("[counts] APPLE   = %d\n", spam_models_count());
	CHECK(spam_models_count() == EXPECTED_APPLE,
	      "APPLE: expected %d, got %d", EXPECTED_APPLE,
	      spam_models_count());

	spam_models_rebuild_active(BT_SPAM_MODE_ANDROID);
	printf("[counts] ANDROID = %d\n", spam_models_count());
	CHECK(spam_models_count() == EXPECTED_ANDROID,
	      "ANDROID: expected %d, got %d", EXPECTED_ANDROID,
	      spam_models_count());

	spam_models_rebuild_active(BT_SPAM_MODE_WINDOWS);
	printf("[counts] WINDOWS = %d\n", spam_models_count());
	CHECK(spam_models_count() == EXPECTED_WINDOWS,
	      "WINDOWS: expected %d, got %d", EXPECTED_WINDOWS,
	      spam_models_count());

	spam_models_rebuild_active(BT_SPAM_MODE_SAMSUNG);
	printf("[counts] SAMSUNG = %d\n", spam_models_count());
	CHECK(spam_models_count() == EXPECTED_SAMSUNG,
	      "SAMSUNG: expected %d, got %d", EXPECTED_SAMSUNG,
	      spam_models_count());

	CHECK(EXPECTED_APPLE + EXPECTED_ANDROID + EXPECTED_WINDOWS +
			      EXPECTED_SAMSUNG ==
		      EXPECTED_TOTAL,
	      "per-mode counts do not add up to %d", EXPECTED_TOTAL);
}

/* (b): len bounds and AD-structure coherence for every model. */
static void test_payload_shape(void)
{
	uint8_t i;
	int checked = 0;

	spam_models_rebuild_active(BT_SPAM_MODE_ALL);
	for (i = 0; i < spam_models_count(); i++) {
		spam_model_t m;
		uint8_t len = spam_models_prepare(i, &m);

		CHECK(m.name != NULL && m.name[0] != '\0',
		      "model %u has no name", (unsigned)i);
		CHECK(len > 0 && len <= SPAM_MODEL_MAX_LEN,
		      "model %u (%s): len %u out of range", (unsigned)i, m.name,
		      (unsigned)len);
		CHECK(m.type <= SPAM_TYPE_SAMSUNG_SETUP,
		      "model %u (%s): bad type", (unsigned)i, m.name);

		if (m.type == SPAM_TYPE_APPLE_PROXIMITY ||
		    m.type == SPAM_TYPE_APPLE_ACTION ||
		    m.type == SPAM_TYPE_SAMSUNG_SETUP) {
			/* Single AD structure: length byte covers the whole
			 * payload. */
			CHECK(len == (uint8_t)(m.data[0] + 1u),
			      "model %u (%s): single-AD len %u != 1+data[0]=%u",
			      (unsigned)i, m.name, (unsigned)len,
			      (unsigned)(m.data[0] + 1u));
		} else {
			/* Multi AD structure (Fast Pair, Swift Pair): the first
			 * structure must fit inside the payload. */
			CHECK((uint8_t)(m.data[0] + 1u) < len,
			      "model %u (%s): first AD structure does not fit "
			      "(%u >= %u)",
			      (unsigned)i, m.name, (unsigned)(m.data[0] + 1u),
			      (unsigned)len);
		}
		checked++;
	}
	CHECK(checked == EXPECTED_TOTAL, "checked %d models, expected %d",
	      checked, EXPECTED_TOTAL);
	printf("[shape]   %d payloads validated (len <= %d, AD coherence)\n",
	       checked, SPAM_MODEL_MAX_LEN);
}

/* (d): randomization varies the payload; same seed reproduces it. */
static void test_randomization(void)
{
	spam_model_t a;
	spam_model_t b;
	spam_model_t c;
	uint8_t len_a;
	uint8_t len_b;

	/* APPLE (first active model = Apple AirPods 1, Proximity 0x07). */
	spam_models_rebuild_active(BT_SPAM_MODE_APPLE);
	spam_models_seed(1234u);
	len_a = spam_models_prepare(0, &a);
	len_b = spam_models_prepare(0, &b);

	CHECK(len_a > 0 && len_a == len_b, "prepare lengths differ: %u vs %u",
	      (unsigned)len_a, (unsigned)len_b);
	CHECK(strcmp(a.name, b.name) == 0, "same position, different model");
	CHECK(memcmp(a.data, b.data, len_a) != 0,
	      "consecutive prepares of %s produced identical payload", a.name);
	printf("[random]  %s: consecutive payloads differ (len %u)\n", a.name,
	       (unsigned)len_a);

	/* Same seed must reproduce the same sequence (seedable PRNG). */
	spam_models_seed(1234u);
	(void)spam_models_prepare(0, &c);
	CHECK(memcmp(a.data, c.data, len_a) == 0, "re-seeding with the same "
						  "value did not reproduce the "
						  "payload");
	printf("[random]  re-seed 1234 reproduces the payload "
	       "deterministically\n");

	/* ANDROID (Fast Pair): no per-type randomization in the reference
	 * implementation; anti-dedup comes from the rotating MAC (Phase 3).
	 * Documented as expected behaviour, not a failure. */
	spam_models_rebuild_active(BT_SPAM_MODE_ANDROID);
	spam_models_seed(1234u);
	len_a = spam_models_prepare(0, &a);
	len_b = spam_models_prepare(0, &b);
	CHECK(len_a > 0 && len_a == len_b, "Fast Pair lengths differ");
	CHECK(memcmp(a.data, b.data, len_a) == 0, "Fast Pair payload must be "
						  "constant (reference "
						  "behaviour)");
	printf("[random]  %s: payload constant by design (MAC rotation only)\n",
	       a.name);
}

/* Supporting checks: index mapping and mode names. */
static void test_index_and_names(void)
{
	uint8_t i;

	spam_models_rebuild_active(BT_SPAM_MODE_WINDOWS);
	CHECK(spam_models_count() == EXPECTED_WINDOWS, "WINDOWS rebuild "
						       "failed");

	/* Every active position must map to a valid table index. */
	for (i = 0; i < spam_models_count(); i++) {
		CHECK(spam_models_index(i) < EXPECTED_TOTAL,
		      "index %u out of table range", (unsigned)i);
	}

	/* Out-of-range positions wrap around (documented API behaviour). */
	CHECK(spam_models_index(spam_models_count()) == spam_models_index(0),
	      "index wrap-around failed");

	CHECK(strcmp(spam_models_mode_name(BT_SPAM_MODE_ALL), "ALL") == 0 &&
		      strcmp(spam_models_mode_name(BT_SPAM_MODE_APPLE),
			     "APPL"
			     "E") == 0 &&
		      strcmp(spam_models_mode_name(BT_SPAM_MODE_ANDROID),
			     "ANDROID") == 0 &&
		      strcmp(spam_models_mode_name(BT_SPAM_MODE_WINDOWS),
			     "WINDOWS") == 0 &&
		      strcmp(spam_models_mode_name(BT_SPAM_MODE_SAMSUNG),
			     "SAMSUNG") == 0,
	      "mode names mismatch");

	/* Apple active list must be Proximity + Action only. */
	spam_models_rebuild_active(BT_SPAM_MODE_APPLE);
	for (i = 0; i < spam_models_count(); i++) {
		spam_model_t m;

		(void)spam_models_prepare(i, &m);
		CHECK(m.type == SPAM_TYPE_APPLE_PROXIMITY ||
			      m.type == SPAM_TYPE_APPLE_ACTION,
		      "non-Apple model %s leaked into APPLE mode", m.name);
	}
	printf("[index]   index mapping, wrap-around and mode names OK\n");
}

int main(void)
{
	spam_models_init();

	test_counts();
	test_payload_shape();
	test_randomization();
	test_index_and_names();

	if (failures == 0) {
		printf("PASS: all ble_spam_models checks succeeded\n");
		return 0;
	}
	printf("FAILED: %d check(s) failed\n", failures);
	return 1;
}
