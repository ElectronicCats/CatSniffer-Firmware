/******************************************************************************

 @file  ble_spam.c

 @brief This file contains the CatSniffer BLE Spam application for use
	with the Bluetooth Low Energy Protocol Stack.

 Group: WCS, BTS
 Target Device: cc13xx_cc26xx

 ******************************************************************************

 Copyright (c) 2013-2023, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************

 *****************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdarg.h>

#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Queue.h>

#include <ti/display/Display.h>
#include <ti/drivers/UART2.h>
#include <ti/drivers/dpl/SystemP.h>

#if (!(defined __TI_COMPILER_VERSION__) && !(defined __GNUC__))
#include <intrinsics.h>
#endif

#include <ti/drivers/GPIO.h>
#include <ti/drivers/TRNG.h>

#include <icall.h>
#include "util.h"
#include <bcomdef.h>
/* This Header file contains all BLE API and icall structure definition */
#include <icall_ble_api.h>

#include <devinfoservice.h>
#include <simple_gatt_profile.h>

#ifdef USE_RCOSC
#include <rcosc_calibration.h>
#endif // USE_RCOSC

#include <ti_drivers_config.h>

#include <ble_spam.h>
#include "ti_ble_config.h"
#include "ble_spam_models.h"

#ifdef PTM_MODE
#include "npi_task.h"	  // To allow RX event registration
#include "npi_ble.h"	  // To enable transmission of messages to UART
#include "icall_hci_tl.h" // To allow ICall HCI Transport Layer
#endif			  // PTM_MODE

/*********************************************************************
 * MACROS
 */
// Spin if the expression is not true
#define BLESPAM_ASSERT(expr) \
	if (!(expr))         \
		BleSpam_spin();

/*********************************************************************
 * CONSTANTS
 */
// Spam engine cycle instrumentation
#define SPAM_CYCLE_EVT_PERIOD 250 // Payload cycle period (ms)
#define SPAM_MAC_ROTATE_CYCLES 8  // Cycles between MAC rotations
#define SPAM_STATS_CYCLES 100	  // Cycles between stats log lines
#define SPAM_LINK_STALL_CYCLES 4  // Stalled cycles before forcing link term

// Task configuration
#define SPAM_TASK_PRIORITY 1

#ifndef SPAM_TASK_STACK_SIZE
#define SPAM_TASK_STACK_SIZE 1024
#endif

// Compile-time default spam mode; override to build per-vendor .hex variants
// without touching the UI, e.g. -DBLE_SPAM_DEFAULT_MODE=BT_SPAM_MODE_APPLE
#ifndef BLE_SPAM_DEFAULT_MODE
#define BLE_SPAM_DEFAULT_MODE BT_SPAM_MODE_ALL
#endif

// Application events
#define SPAM_STATE_CHANGE_EVT 0
#define SPAM_CHAR_CHANGE_EVT 1
#define SPAM_KEY_CHANGE_EVT 2
#define SPAM_ADV_EVT 3
#define SPAM_PAIR_STATE_EVT 4
#define SPAM_PASSCODE_EVT 5
#define SPAM_PERIODIC_EVT 6
#define SPAM_SEND_PARAM_UPDATE_EVT 7
#define SPAM_CONN_EVT 8

#define SPAM_CYCLE_EVT 9
#define SPAM_UART_CMD_EVT 10

// Internal Events for RTOS application
#define SPAM_ICALL_EVT ICALL_MSG_EVENT_ID  // Event_Id_31
#define SPAM_QUEUE_EVT UTIL_QUEUE_EVENT_ID // Event_Id_30

// Bitwise OR of all RTOS events to pend on
#define SPAM_ALL_EVENTS (SPAM_ICALL_EVT | SPAM_QUEUE_EVT)

// Size of string-converted device address ("0xXXXXXXXXXXXX")
#define SPAM_ADDR_STR_SIZE 15

/*********************************************************************
 * TYPEDEFS
 */

// App event passed from stack modules. This type is defined by the application
// since it can queue events to itself however it wants.
typedef struct {
	uint8_t event; // event type
	void *pData;   // pointer to message
} spamEvt_t;

// Container to store passcode data when passing from gapbondmgr callback
// to app event. See the pfnPairStateCB_t documentation from the gapbondmgr.h
// header file for more information on each parameter.
typedef struct {
	uint8_t state;
	uint16_t connHandle;
	uint8_t status;
} spamPairStateData_t;

// Container to store passcode data when passing from gapbondmgr callback
// to app event. See the pfnPasscodeCB_t documentation from the gapbondmgr.h
// header file for more information on each parameter.
typedef struct {
	uint8_t deviceAddr[B_ADDR_LEN];
	uint16_t connHandle;
	uint8_t uiInputs;
	uint8_t uiOutputs;
	uint32_t numComparison;
} spamPasscodeData_t;

// Container to store advertising event data when passing from advertising
// callback to app event. See the respective event in GapAdvScan_Event_IDs
// in gap_advertiser.h for the type that pBuf should be cast to.
typedef struct {
	uint32_t event;
	void *pBuf;
} spamGapAdvEventData_t;

// Container to store information from clock expiration using a flexible array
// since data is not always needed
typedef struct {
	uint8_t event; //
	uint8_t data[];
} spamClockEventData_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

// Kept as a NULL handle only so the stack AssertHandler in Startup/main.c
// links. Normal status output goes over UART2 (BleSpam_log); in an assert
// its Display_open() fails gracefully because UART2 already owns the port.
Display_Handle dispHandle = NULL;

// Console UART: status output plus single-line command input. The
// CatSniffer v3 has no user buttons, so the run mode is driven over UART.
#define SPAM_CMD_MAXLEN 24
// Single-producer (RX callback) / single-consumer (task) ring of completed
// command lines. The host sends several commands back-to-back with no pacing
// (mode -> start -> status), so the RX path must keep accepting the next line
// while the task is still handling the previous one. SPAM_CMD_QUEUE_N slots is
// enough to hold one such burst even while BleSpam_start() is busy.
#define SPAM_CMD_QUEUE_N 4
static UART2_Handle uartHandle = NULL;
static uint8_t spamRxByte;
static char spamCmdQ[SPAM_CMD_QUEUE_N][SPAM_CMD_MAXLEN];
static volatile uint8_t spamQHead = 0; // task advances after consuming a line
static volatile uint8_t spamQTail = 0; // callback advances after a line
				       // completes
static uint8_t spamRxLen = 0;	       // bytes accumulated in the tail slot

// Task configuration
Task_Struct spamTask;
#if defined __TI_COMPILER_VERSION__
#pragma DATA_ALIGN(spamTaskStack, 8)
#else
#pragma data_alignment = 8
#endif
uint8_t spamTaskStack[SPAM_TASK_STACK_SIZE];

#define APP_EVT_EVENT_MAX 0xA
char *appEventStrings[] = {
	"APP_STATE_CHANGE_EVT     ", "APP_CHAR_CHANGE_EVT      ",
	"APP_KEY_CHANGE_EVT       ", "APP_ADV_EVT              ",
	"APP_PAIR_STATE_EVT       ", "APP_PASSCODE_EVT         ",
	"APP_PERIODIC_EVT         ", "APP_SEND_PARAM_UPDATE_EVT",
	"APP_CONN_EVT             ", "APP_SPAM_CYCLE_EVT       ",
	"APP_UART_CMD_EVT         ",
};

/*********************************************************************
 * LOCAL VARIABLES
 */

// Entity ID globally used to check for source and/or destination of messages
static ICall_EntityID selfEntity;

// Event globally used to post local events and pend on system and
// local events.
static ICall_SyncHandle syncEvent;

// Queue object used for app messages
static Queue_Struct appMsgQueue;
static Queue_Handle appMsgQueueHandle;

// One-shot clock for the spam payload cycle
static Clock_Struct clkSpamCycle;

// Memory to pass the spam cycle event ID to the clock handler
spamClockEventData_t argSpamCycle = { .event = SPAM_CYCLE_EVT };

// Advertising handles
static uint8 advHandleLegacy;

// Address mode: static random address (0xC0), rotated every 8 cycles
static GAP_Addr_Modes_t addrMode = ADDRMODE_RANDOM;

// Spam engine state
static bool spamAdvReady; // Adv set created (GAP init done)
static bool spamRunning;  // 250 ms cycle engine active
static bool spamTrngOk;	  // TRNG seed available
static bt_spam_mode_t spamMode = BLE_SPAM_DEFAULT_MODE;
static uint8_t spamActiveIdx;	    // Position in the active model list
static uint16_t spamCycleCount;	    // Cycles since BleSpam_start()
static uint8_t spamLinkStallCycles; // Consecutive cycles blocked by a link
static bool spamRotateEveryCycle; // Rotate MAC per cycle (Fase 2, no ephemeral
				  // payload)
static spam_model_t spamCurModel; // Stack keeps the pointer after load
static uint8_t spamAddr[B_ADDR_LEN]; // Rotating static random address
static uint32_t spamAddrRng;	     // xorshift32 state for addresses

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void BleSpam_init(void);
static void BleSpam_taskFxn(UArg a0, UArg a1);

static uint8_t BleSpam_processStackMsg(ICall_Hdr *pMsg);
static uint8_t BleSpam_processGATTMsg(gattMsgEvent_t *pMsg);
static void BleSpam_processGapMessage(gapEventHdr_t *pMsg);
static void BleSpam_advCallback(uint32_t event, void *pBuf, uintptr_t arg);
static void BleSpam_processAdvEvent(spamGapAdvEventData_t *pEventData);
static void BleSpam_processAppMsg(spamEvt_t *pMsg);
static void BleSpam_processCharValueChangeEvt(uint8_t paramId);
static void BleSpam_clockHandler(UArg arg);
static void BleSpam_passcodeCb(uint8_t *pDeviceAddr, uint16_t connHandle,
			       uint8_t uiInputs, uint8_t uiOutputs,
			       uint32_t numComparison);
static void BleSpam_pairStateCb(uint16_t connHandle, uint8_t state,
				uint8_t status);
static void BleSpam_processPairState(spamPairStateData_t *pPairState);
static void BleSpam_processPasscode(spamPasscodeData_t *pPasscodeData);
static void BleSpam_charValueChangeCB(uint8_t paramId);
static status_t BleSpam_enqueueMsg(uint8_t event, void *pData);
static void BleSpam_initRandom(void);
static void BleSpam_processSpamCycle(void);
static void BleSpam_swapPayload(const spam_model_t *pModel, bool rotateAddr);
static void BleSpam_rotateAddress(void);
static uint32_t BleSpam_addrRand(void);
static void BleSpam_logErr(const char *what, bStatus_t status);
static void BleSpam_logAddr(const char *tag, const uint8_t *addr);
static void BleSpam_log(const char *fmt, ...);
static void BleSpam_uartReadCb(UART2_Handle handle, void *buf, size_t count,
			       void *userArg, int_fast16_t status);
static void BleSpam_handleCommand(const char *cmd);

/*********************************************************************
 * EXTERN FUNCTIONS
 */
extern void AssertHandler(uint8 assertCause, uint8 assertSubcause);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Bond Manager Callbacks
static gapBondCBs_t BleSpam_BondMgrCBs = {
	BleSpam_passcodeCb, // Passcode callback
	BleSpam_pairStateCb // Pairing/Bonding state Callback
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t BleSpam_simpleProfileCBs = {
	BleSpam_charValueChangeCB // Simple GATT Characteristic value
				  // change callback
};

/*********************************************************************
 * @fn      BleSpam_createTask
 *
 * @brief   Task creation function for the Simple Peripheral.
 */
void BleSpam_createTask(void)
{
	Task_Params taskParams;

	// Configure task
	Task_Params_init(&taskParams);
	taskParams.stack = spamTaskStack;
	taskParams.stackSize = SPAM_TASK_STACK_SIZE;
	taskParams.priority = SPAM_TASK_PRIORITY;

	Task_construct(&spamTask, BleSpam_taskFxn, &taskParams, NULL);
}

/*********************************************************************
 * @fn      BleSpam_start
 *
 * @brief   Rebuild the active model list for the given mode, load the
 *          first payload and arm the 250 ms advertising cycle.
 *
 * @param   mode - spam mode to run (BT_SPAM_MODE_ALL, ...)
 *
 * @return  None.
 */
void BleSpam_start(bt_spam_mode_t mode)
{
	bStatus_t advStatus;

	spamMode = mode;
	spam_models_rebuild_active(spamMode);
	spamActiveIdx = 0;
	spamCycleCount = 0;
	spamLinkStallCycles = 0;

	// Fase 2 anti-dedup: Fast Pair (discoverable model ID) and Samsung
	// EasySetup carry no per-cycle ephemeral payload field, so the receiver
	// dedups a fixed model unless the MAC changes. Rotate the static random
	// address every cycle for the modes that contain those models. Apple
	// and Windows keep the 8-cycle cadence (Apple already randomizes
	// ephemeral payload bytes) to limit radio churn.
	spamRotateEveryCycle = (mode == BT_SPAM_MODE_ANDROID ||
				mode == BT_SPAM_MODE_SAMSUNG ||
				mode == BT_SPAM_MODE_ALL);

	BleSpam_log("SPAM: start mode=%s models=%u int=20-30ms rot=%s",
		    spam_models_mode_name(spamMode),
		    (unsigned)spam_models_count(),
		    spamRotateEveryCycle ? "cycle" : "8-cycle");

	spam_models_prepare(spamActiveIdx, &spamCurModel);
	BleSpam_log("SPAM: model %s (1/%u)", spamCurModel.name,
		    (unsigned)spam_models_count());
	BleSpam_logAddr("SPAM: addr", spamAddr);

	if (!spamAdvReady) {
		return;
	}

	spamRunning = true;
	BleSpam_swapPayload(&spamCurModel, false);

	// The swap usually re-enabled the set already; belt and braces
	advStatus = GapAdv_enable(advHandleLegacy,
				  GAP_ADV_ENABLE_OPTIONS_USE_MAX, 0);
	if ((advStatus != SUCCESS) &&
	    (advStatus != bleAlreadyInRequestedMode)) {
		BleSpam_logErr("adv enable", advStatus);
	}

	Util_startClock(&clkSpamCycle);
}

/*********************************************************************
 * @fn      BleSpam_stop
 *
 * @brief   Disarm the 250 ms cycle clock and disable advertising.
 *
 * @return  None.
 */
void BleSpam_stop(void)
{
	if (!spamRunning) {
		return;
	}

	spamRunning = false;
	Util_stopClock(&clkSpamCycle);
	GapAdv_disable(advHandleLegacy);
}

/*********************************************************************
 * @fn      BleSpam_is_running
 *
 * @brief   Report whether the 250 ms advertising cycle is running.
 *
 * @return  true while running.
 */
bool BleSpam_is_running(void)
{
	return spamRunning;
}

/*********************************************************************
 * @fn      BleSpam_logErr
 *
 * @brief   Print "ERR: <what> <status>" on the display.
 *
 * @param   what   - context of the error
 * @param   status - bStatus_t returned by the stack
 *
 * @return  None.
 */
static void BleSpam_logErr(const char *what, bStatus_t status)
{
	BleSpam_log("ERR: %s %d", what, (int)status);
}

/*********************************************************************
 * @fn      BleSpam_logAddr
 *
 * @brief   Print a Bluetooth address, most significant byte first.
 *
 * @param   tag  - prefix of the line
 * @param   addr - 6-byte address (little-endian on air)
 *
 * @return  None.
 */
static void BleSpam_logAddr(const char *tag, const uint8_t *addr)
{
	BleSpam_log("%s %02x:%02x:%02x:%02x:%02x:%02x", tag, (unsigned)addr[5],
		    (unsigned)addr[4], (unsigned)addr[3], (unsigned)addr[2],
		    (unsigned)addr[1], (unsigned)addr[0]);
}

/*********************************************************************
 * @fn      BleSpam_log
 *
 * @brief   Formatted status line over the console UART. Appends CRLF,
 *          matching the one-line-per-call behaviour of Display_printf.
 *          Called from task context only (blocking UART writes).
 *
 * @param   fmt - printf-style format string
 *
 * @return  None.
 */
static void BleSpam_log(const char *fmt, ...)
{
	char buf[132];
	va_list ap;
	int n;

	if (uartHandle == NULL) {
		return;
	}

	va_start(ap, fmt);
	n = SystemP_vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
	va_end(ap);

	if (n < 0) {
		return;
	}
	// C1: SystemP_vsnprintf writes at most sizeof(buf)-2 chars plus its
	// NUL, and returns the untruncated length. Clamp to sizeof(buf)-3 so
	// the two CRLF bytes overwrite the trailing content/NUL instead of
	// leaving an embedded NUL at buf[sizeof(buf)-2] and dropping the last
	// character.
	if (n > (int)(sizeof(buf) - 3)) {
		n = (int)(sizeof(buf) - 3);
	}
	buf[n++] = '\r';
	buf[n++] = '\n';

	UART2_write(uartHandle, buf, (size_t)n, NULL);
}

/*********************************************************************
 * @fn      BleSpam_uartReadCb
 *
 * @brief   UART RX callback (may run in HWI/SWI context). Accumulates bytes
 *          straight into the current ring slot; on CR/LF it commits the line
 *          (advances the tail) and signals the task via SPAM_UART_CMD_EVT, then
 *          re-arms the next single-byte read. Because it keeps receiving into
 *          the next slot regardless of whether the task has drained the queue,
 *          back-to-back commands are no longer lost. A line is dropped only if
 *          the ring is full (SPAM_CMD_QUEUE_N lines pending) or overruns the
 *          per-line length cap.
 *
 * @return  None.
 */
static void BleSpam_uartReadCb(UART2_Handle handle, void *buf, size_t count,
			       void *userArg, int_fast16_t status)
{
	(void)handle;
	(void)buf;
	(void)userArg;

	if ((status == UART2_STATUS_SUCCESS) && (count > 0)) {
		char c = (char)spamRxByte;

		if ((c == '\n') || (c == '\r')) {
			if (spamRxLen > 0) {
				uint8_t next =
					(spamQTail + 1) % SPAM_CMD_QUEUE_N;
				// Commit only if the ring has a free slot;
				// otherwise drop this line and keep the queue.
				if (next != spamQHead) {
					spamCmdQ[spamQTail][spamRxLen] = '\0';
					spamQTail = next;
					BleSpam_enqueueMsg(SPAM_UART_CMD_EVT,
							   NULL);
				}
			}
			spamRxLen = 0;
		} else if (spamRxLen < (SPAM_CMD_MAXLEN - 1)) {
			spamCmdQ[spamQTail][spamRxLen++] = c;
		}
	}

	// Re-arm the next single-byte read to keep receiving.
	if (uartHandle != NULL) {
		UART2_read(uartHandle, &spamRxByte, 1, NULL);
	}
}

/*********************************************************************
 * @fn      BleSpam_handleCommand
 *
 * @brief   Parse and act on a completed UART command line (task context).
 *          Replaces the button UX the CatSniffer v3 hardware cannot offer.
 *          Modes: all/apple/android/windows/samsung (or a/p/n/w/s).
 *          Control: start (r), stop (x), status (st), help (?).
 *
 * @param   cmd - NUL-terminated command word
 *
 * @return  None.
 */
static void BleSpam_handleCommand(const char *cmd)
{
	bt_spam_mode_t mode;
	bool haveMode = true;

	if ((strcmp(cmd, "all") == 0) || (strcmp(cmd, "a") == 0)) {
		mode = BT_SPAM_MODE_ALL;
	} else if ((strcmp(cmd, "apple") == 0) || (strcmp(cmd, "p") == 0)) {
		mode = BT_SPAM_MODE_APPLE;
	} else if ((strcmp(cmd, "android") == 0) || (strcmp(cmd, "n") == 0)) {
		mode = BT_SPAM_MODE_ANDROID;
	} else if ((strcmp(cmd, "windows") == 0) || (strcmp(cmd, "w") == 0)) {
		mode = BT_SPAM_MODE_WINDOWS;
	} else if ((strcmp(cmd, "samsung") == 0) || (strcmp(cmd, "s") == 0)) {
		mode = BT_SPAM_MODE_SAMSUNG;
	} else {
		haveMode = false;
	}

	if (haveMode) {
		if (BleSpam_is_running()) {
			// Restart the cycle on the new mode's model list
			BleSpam_start(mode);
		} else {
			spamMode = mode;
			spam_models_rebuild_active(spamMode);
			BleSpam_log("SPAM: mode -> %s (stopped)",
				    spam_models_mode_name(spamMode));
		}
		return;
	}

	if ((strcmp(cmd, "stop") == 0) || (strcmp(cmd, "x") == 0)) {
		BleSpam_stop();
		BleSpam_log("SPAM: stopped");
	} else if ((strcmp(cmd, "start") == 0) || (strcmp(cmd, "run") == 0) ||
		   (strcmp(cmd, "r") == 0)) {
		if (BleSpam_is_running()) {
			BleSpam_log("SPAM: already running");
		} else {
			BleSpam_start(spamMode);
		}
	} else if ((strcmp(cmd, "status") == 0) || (strcmp(cmd, "st") == 0)) {
		BleSpam_log("SPAM: mode=%s running=%u models=%u rot=%s",
			    spam_models_mode_name(spamMode),
			    (unsigned)BleSpam_is_running(),
			    (unsigned)spam_models_count(),
			    spamRotateEveryCycle ? "cycle" : "8-cycle");
	} else if ((strcmp(cmd, "help") == 0) || (strcmp(cmd, "?") == 0)) {
		BleSpam_log("SPAM cmds: all|apple|android|windows|samsung, "
			    "start, stop, status");
	} else {
		BleSpam_log("ERR: unknown cmd '%s' (type help)", cmd);
	}
}

/*********************************************************************
 * @fn      BleSpam_addrRand
 *
 * @brief   xorshift32 step used for the rotating random address.
 *
 * @return  Next pseudo-random word (never 0).
 */
static uint32_t BleSpam_addrRand(void)
{
	uint32_t x = spamAddrRng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;

	if (x == 0) {
		x = 0xA5A5A5A5u;
	}

	spamAddrRng = x;
	return x;
}

/*********************************************************************
 * @fn      BleSpam_rotateAddress
 *
 * @brief   Generate a new static random address (two MSBs set to 11)
 *          and push it to the controller.
 *
 * @return  None.
 */
static void BleSpam_rotateAddress(void)
{
	uint8_t i;
	hciStatus_t status;

	for (i = 0; i < (B_ADDR_LEN - 1); i++) {
		spamAddr[i] = (uint8_t)BleSpam_addrRand();
	}

	// Byte 5 holds the two most significant bits (little-endian)
	spamAddr[B_ADDR_LEN - 1] =
		(uint8_t)((uint8_t)BleSpam_addrRand() | 0xC0);

	status = HCI_LE_SetRandomAddressCmd(spamAddr);
	if (status != SUCCESS) {
		BleSpam_logErr("set random addr", status);
	}
}

/*********************************************************************
 * @fn      BleSpam_initRandom
 *
 * @brief   One-shot TRNG read: model PRNG seed (4 B), address PRNG
 *          seed (4 B) and initial static random address (6 B). Falls
 *          back to fixed values when the TRNG is unavailable (D9).
 *
 * @return  None.
 */
static void BleSpam_initRandom(void)
{
	static const uint8_t fallbackAddr[B_ADDR_LEN] = { 0x11, 0x22, 0x33,
							  0x44, 0x55, 0xC0 };
	uint8_t rnd[14]; // 4 model seed + 4 addr seed + 6 addr
	uint32_t modelSeed;
	uint32_t addrSeed;
	TRNG_Handle trngHandle = TRNG_open(CONFIG_TRNG_0, NULL);

	spamTrngOk = false;

	if ((trngHandle != NULL) &&
	    (TRNG_getRandomBytes(trngHandle, rnd, sizeof(rnd)) ==
	     TRNG_STATUS_SUCCESS)) {
		spamTrngOk = true;
	}

	if (trngHandle != NULL) {
		TRNG_close(trngHandle);
	}

	if (spamTrngOk) {
		modelSeed = (uint32_t)rnd[0] | ((uint32_t)rnd[1] << 8) |
			    ((uint32_t)rnd[2] << 16) | ((uint32_t)rnd[3] << 24);
		addrSeed = (uint32_t)rnd[4] | ((uint32_t)rnd[5] << 8) |
			   ((uint32_t)rnd[6] << 16) | ((uint32_t)rnd[7] << 24);

		spam_models_seed(modelSeed);
		spamAddrRng = (addrSeed != 0) ? addrSeed : 0x9E3779B9u;

		memcpy(spamAddr, &rnd[8], B_ADDR_LEN);
		spamAddr[B_ADDR_LEN - 1] |= 0xC0;
	} else {
		// C2: the TRNG is unavailable (reported by BleSpam_init()). A
		// fully constant fallback would replay the same PRNG stream and
		// the same MAC on every boot. Fold in live entropy from the
		// RTOS tick counter so seeds and the fallback address
		// decorrelate across boots. Not cryptographic; the full
		// per-cycle hardening lands in Fase 2.
		uint32_t entropy = (uint32_t)Clock_getTicks();

		entropy ^= entropy << 13;
		entropy ^= entropy >> 17;
		entropy ^= entropy << 5;

		spam_models_seed(0xC5D5E7A1u ^ entropy);
		spamAddrRng = 0x5A3C2709u ^ entropy;
		if (spamAddrRng == 0) {
			spamAddrRng = 0x9E3779B9u;
		}

		memcpy(spamAddr, fallbackAddr, B_ADDR_LEN);
		spamAddr[0] ^= (uint8_t)entropy;
		spamAddr[1] ^= (uint8_t)(entropy >> 8);
		spamAddr[2] ^= (uint8_t)(entropy >> 16);
		spamAddr[3] ^= (uint8_t)(entropy >> 24);
		spamAddr[B_ADDR_LEN - 1] |= 0xC0; // Keep static-random top bits
	}
}

/*********************************************************************
 * @fn      BleSpam_swapPayload
 *
 * @brief   Load pModel as the advertising payload, optionally
 *          rotating the MAC inside the prepare/load window (D7).
 *
 * @param   pModel     - model whose randomized payload is loaded
 * @param   rotateAddr - rotate the static random address first
 *
 * @return  None.
 */
static void BleSpam_swapPayload(const spam_model_t *pModel, bool rotateAddr)
{
	bStatus_t status;

	status = GapAdv_prepareLoadByHandle(advHandleLegacy,
					    GAP_ADV_FREE_OPTION_DONT_FREE);

	if (status == SUCCESS) {
		// Set disabled by prepare: change addr then load, which
		// re-enables the set automatically (stop -> addr -> start)
		if (rotateAddr) {
			BleSpam_rotateAddress();
		}

		spamCurModel = *pModel;
		status = GapAdv_loadByHandle(advHandleLegacy,
					     GAP_ADV_DATA_TYPE_ADV,
					     spamCurModel.len,
					     spamCurModel.data);
	} else if (status == bleAlreadyInRequestedMode) {
		// Set not advertising (D10): plan fallback
		// disable -> load -> enable
		GapAdv_disable(advHandleLegacy);

		if (rotateAddr) {
			BleSpam_rotateAddress();
		}

		spamCurModel = *pModel;
		status = GapAdv_loadByHandle(advHandleLegacy,
					     GAP_ADV_DATA_TYPE_ADV,
					     spamCurModel.len,
					     spamCurModel.data);

		if (status == SUCCESS) {
			status = GapAdv_enable(advHandleLegacy,
					       GAP_ADV_ENABLE_OPTIONS_USE_MAX,
					       0);
			if (status == bleAlreadyInRequestedMode) {
				status = SUCCESS;
			}
		}
	} else {
		// Diagnose R1/R2 (bleIncorrectMode, bleGAPBufferInUse, ...)
		if (status == bleIncorrectMode) {
			// GapAdv_abortLoad() macro leaves a dangling comma
			// with zero args on this compiler: expand it by hand
			(void)icall_directAPI(ICALL_SERVICE_CLASS_BLE,
					      (uint32_t)IDX_GapAdv_abortLoad);
		}
	}

	if (status != SUCCESS) {
		BleSpam_logErr("adv swap", status);
	}
}

/*********************************************************************
 * @fn      BleSpam_processSpamCycle
 *
 * @brief   Advance the active model, randomize and load its payload,
 *          rotate the MAC every 8 cycles and rearm the 250 ms clock.
 *
 * @return  None.
 */
static void BleSpam_processSpamCycle(void)
{
	bool rotateAddr;

	if (!spamRunning) {
		return;
	}

	// Incoming links take the radio: skip the swap but keep the
	// engine alive (the link is terminated by the GAP handler).
	if (linkDB_NumActive() > 0) {
		// C5: if a link persists across several cycles the GAP
		// handler's termination did not take; force-terminate every
		// active link so the engine cannot stall indefinitely on a
		// stuck connection.
		if (++spamLinkStallCycles >= SPAM_LINK_STALL_CYCLES) {
			bStatus_t termStatus = GAP_TerminateLinkReq(
				LINKDB_CONNHANDLE_ALL,
				HCI_DISCONNECT_REMOTE_USER_TERM);
			if (termStatus != SUCCESS) {
				BleSpam_logErr("stall term", termStatus);
			}
			spamLinkStallCycles = 0;
		}
		Util_startClock(&clkSpamCycle);
		return;
	}
	spamLinkStallCycles = 0;

	spamActiveIdx = (uint8_t)((spamActiveIdx + 1) % spam_models_count());
	spam_models_prepare(spamActiveIdx, &spamCurModel);

	spamCycleCount++;
	rotateAddr = spamRotateEveryCycle ||
		     ((spamCycleCount % SPAM_MAC_ROTATE_CYCLES) == 0);

	BleSpam_swapPayload(&spamCurModel, rotateAddr);
	Util_startClock(&clkSpamCycle);

	BleSpam_log("SPAM: %s (%u/%u)", spamCurModel.name,
		    (unsigned)(spamActiveIdx + 1),
		    (unsigned)spam_models_count());

	if ((spamCycleCount % SPAM_STATS_CYCLES) == 0) {
		BleSpam_log("SPAM: cycles=%u", (unsigned)spamCycleCount);
	}
}

/*********************************************************************
 * @fn      BleSpam_spin
 *
 * @brief   Spin forever
 *
 * @param   none
 */
static void BleSpam_spin(void)
{
	volatile uint8_t x;

	while (1) {
		x++;
	}
}

/*********************************************************************
 * @fn      BleSpam_init
 *
 * @brief   Called during initialization and contains application
 *          specific initialization (ie. hardware initialization/setup,
 *          table initialization, power up notification, etc), and
 *          profile initialization/setup.
 */
static void BleSpam_init(void)
{
	BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- init ",
			 SPAM_TASK_PRIORITY);

	// ******************************************************************
	// N0 STACK API CALLS CAN OCCUR BEFORE THIS CALL TO ICall_registerApp
	// ******************************************************************
	// Register the current thread as an ICall dispatcher application
	// so that the application can send and receive messages.
	ICall_registerApp(&selfEntity, &syncEvent);

#ifdef USE_RCOSC
	// Set device's Sleep Clock Accuracy
#if (HOST_CONFIG & (CENTRAL_CFG | PERIPHERAL_CFG))
	HCI_EXT_SetSCACmd(500);
#endif // (CENTRAL_CFG | PERIPHERAL_CFG)
	RCOSC_enableCalibration();
#endif // USE_RCOSC

	// Create an RTOS queue for message from profile to be sent to app.
	appMsgQueueHandle = Util_constructQueue(&appMsgQueue);

	// Create one-shot clock for the spam payload cycle
	Util_constructClock(&clkSpamCycle, BleSpam_clockHandler,
			    SPAM_CYCLE_EVT_PERIOD, 0, false,
			    (UArg)&argSpamCycle);

	// Set the Device Name characteristic in the GAP GATT Service
	// For more information, see the section in the User's Guide:
	// http://software-dl.ti.com/lprf/ble5stack-latest/
	GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN,
			 attDeviceName);

	// Configure GAP
	{
		uint16_t paramUpdateDecision =
			DEFAULT_PARAM_UPDATE_REQ_DECISION;

		// Pass all parameter update requests to the app for it to
		// decide
		GAP_SetParamValue(GAP_PARAM_LINK_UPDATE_DECISION,
				  paramUpdateDecision);
	}

	// Setup the GAP Bond Manager. For more information see the GAP Bond
	// Manager section in the User's Guide
	setBondManagerParameters();

	// Initialize GATT attributes
	GGS_AddService(GAP_SERVICE);		   // GAP GATT Service
	GATTServApp_AddService(GATT_ALL_SERVICES); // GATT Service
	DevInfo_AddService();			   // Device Information Service
	SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile

	// Setup the SimpleProfile Characteristic Values
	// For more information, see the GATT and GATTServApp sections in the
	// User's Guide: http://software-dl.ti.com/lprf/ble5stack-latest/
	{
		uint8_t charValue1 = 1;
		uint8_t charValue2 = 2;
		uint8_t charValue3 = 3;
		uint8_t charValue4 = 4;
		uint8_t charValue5[SIMPLEPROFILE_CHAR5_LEN] = { 1, 2, 3, 4, 5 };

		SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, sizeof(uint8_t),
					   &charValue1);
		SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, sizeof(uint8_t),
					   &charValue2);
		SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, sizeof(uint8_t),
					   &charValue3);
		SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, sizeof(uint8_t),
					   &charValue4);
		SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5,
					   SIMPLEPROFILE_CHAR5_LEN, charValue5);
	}

	// Register callback with SimpleGATTprofile
	SimpleProfile_RegisterAppCBs(&BleSpam_simpleProfileCBs);

	// Start Bond Manager and register callback
	VOID GAPBondMgr_Register(&BleSpam_BondMgrCBs);

	// Register with GAP for HCI/Host messages. This is needed to receive
	// HCI events. For more information, see the HCI section in the User's
	// Guide: http://software-dl.ti.com/lprf/ble5stack-latest/
	GAP_RegisterForMsgs(selfEntity);

	// Register for GATT local events and ATT Responses pending for
	// transmission
	GATT_RegisterForMsgs(selfEntity);

	// Set default values for Data Length Extension
	// Extended Data Length Feature is already enabled by default
	{
// Set initial values to maximum, RX is set to max. by default(251 octets,
// 2120us) Some brand smartphone is essentially needing 251/2120, so we set them
// here.
#define APP_SUGGESTED_PDU_SIZE 251 // default is 27 octets(TX)
#define APP_SUGGESTED_TX_TIME 2120 // default is 328us(TX)

		// This API is documented in hci.h
		// See the LE Data Length Extension section in the BLE5-Stack
		// User's Guide for information on using this command:
		// http://software-dl.ti.com/lprf/ble5stack-latest/
		HCI_LE_WriteSuggestedDefaultDataLenCmd(APP_SUGGESTED_PDU_SIZE,
						       APP_SUGGESTED_TX_TIME);
	}

	// Initialize GATT Client
	GATT_InitClient();

	// Source address: seed the model PRNG and the rotating static
	// random address from the TRNG (fixed fallback on error)
	spam_models_init();
	BleSpam_initRandom();
	uint8 *pDevRandAddr = (uint8 *)spamAddr;

	BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP,
			 "APP : ---- call GAP_DeviceInit",
			 GAP_PROFILE_PERIPHERAL);
	// Initialize GAP layer for Peripheral role and register to receive GAP
	// events
	GAP_DeviceInit(GAP_PROFILE_PERIPHERAL, selfEntity, addrMode,
		       pDevRandAddr);

	// Open the console UART directly (the same UART the Display module
	// maps) so we can both print status and receive single-line commands.
	// The Display driver is output only; the CatSniffer v3 has no user
	// buttons, so the run mode has to be driven over UART instead.
	{
		UART2_Params uartParams;

		UART2_Params_init(&uartParams);
		uartParams.baudRate = 921600;
		uartParams.writeMode = UART2_Mode_BLOCKING;
		uartParams.readMode = UART2_Mode_CALLBACK;
		uartParams.readCallback = BleSpam_uartReadCb;
		uartParams.readReturnMode = UART2_ReadReturnMode_PARTIAL;
		uartHandle = UART2_open(CONFIG_DISPLAY_UART, &uartParams);
		if (uartHandle != NULL) {
			// Arm the first single-byte read; the callback re-arms.
			UART2_read(uartHandle, &spamRxByte, 1, NULL);
		}
	}

	// Report the fixed-seed fallback once the UART is open
	if (!spamTrngOk) {
		BleSpam_log("ERR: TRNG unavailable, fixed seed used");
	}

	// The CatSniffer v3 has no user buttons: runtime control is over UART
	// (see BleSpam_handleCommand): "help" lists the commands.
	BleSpam_log("SPAM: UART control ready (type 'help')");
}

/*********************************************************************
 * @fn      BleSpam_taskFxn
 *
 * @brief   Application task entry point for the Simple Peripheral.
 *
 * @param   a0, a1 - not used.
 */
static void BleSpam_taskFxn(UArg a0, UArg a1)
{
	// Initialize application
	BleSpam_init();
	BleSpam_log("CatSniffer:BleSpam %s", spam_models_mode_name(spamMode));
	// Application main loop
	for (;;) {
		uint32_t events;

		// Waits for an event to be posted associated with the calling
		// thread. Note that an event associated with a thread is posted
		// when a message is queued to the message receive queue of the
		// thread
		events = Event_pend(syncEvent, Event_Id_NONE, SPAM_ALL_EVENTS,
				    ICALL_TIMEOUT_FOREVER);

		if (events) {
			ICall_EntityID dest;
			ICall_ServiceEnum src;
			ICall_HciExtEvt *pMsg = NULL;

			// Fetch any available messages that might have been
			// sent from the stack
			if (ICall_fetchServiceMsg(&src, &dest,
						  (void **)&pMsg) ==
			    ICALL_ERRNO_SUCCESS) {
				uint8 safeToDealloc = TRUE;

				if ((src == ICALL_SERVICE_CLASS_BLE) &&
				    (dest == selfEntity)) {
					ICall_Stack_Event *pEvt =
						(ICall_Stack_Event *)pMsg;

					// Check for BLE stack events first
					if (pEvt->signature != 0xffff) {
						// Process inter-task message
						safeToDealloc =
							BleSpam_processStackMsg(
								(ICall_Hdr *)
									pMsg);
					}
				}

				if (pMsg && safeToDealloc) {
					ICall_freeMsg(pMsg);
				}
			}

			// If RTOS queue is not empty, process app message.
			if (events & SPAM_QUEUE_EVT) {
				while (!Queue_empty(appMsgQueueHandle)) {
					spamEvt_t *pMsg =
						(spamEvt_t *)Util_dequeueMsg(
							appMsgQueueHandle);
					if (pMsg) {
						// Process message.
						BleSpam_processAppMsg(pMsg);

						// Free the space from the
						// message.
						ICall_free(pMsg);
					}
				}
			}
		}
	}
}

/*********************************************************************
 * @fn      BleSpam_processStackMsg
 *
 * @brief   Process an incoming stack message.
 *
 * @param   pMsg - message to process
 *
 * @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
 */
static uint8_t BleSpam_processStackMsg(ICall_Hdr *pMsg)
{
	// Always dealloc pMsg unless set otherwise
	uint8_t safeToDealloc = TRUE;

	BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP,
			"APP : Stack msg status=%d, event=0x%x\n", pMsg->status,
			pMsg->event);

	switch (pMsg->event) {
	case GAP_MSG_EVENT:
		BleSpam_processGapMessage((gapEventHdr_t *)pMsg);
		break;

	case GATT_MSG_EVENT:
		// Process GATT message
		safeToDealloc = BleSpam_processGATTMsg((gattMsgEvent_t *)pMsg);
		break;

	case HCI_GAP_EVENT_EVENT: {
		// Process HCI message. The spam engine terminates every
		// incoming link immediately, so there is no PHY/RSSI or
		// command-status handling to do; only the hardware error is
		// meaningful.
		if (pMsg->status == HCI_BLE_HARDWARE_ERROR_EVENT_CODE) {
			AssertHandler(HAL_ASSERT_CAUSE_HARDWARE_ERROR, 0);
		}

		break;
	}

	default:
		// do nothing
		break;
	}

	return (safeToDealloc);
}

/*********************************************************************
 * @fn      BleSpam_processGATTMsg
 *
 * @brief   Process GATT messages and events.
 *
 * @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
 */
static uint8_t BleSpam_processGATTMsg(gattMsgEvent_t *pMsg)
{
	if (pMsg->method == ATT_FLOW_CTRL_VIOLATED_EVENT) {
		// ATT request-response or indication-confirmation flow control
		// is violated. All subsequent ATT requests or indications will
		// be dropped. The app is informed in case it wants to drop the
		// connection.

		// Display the opcode of the message that caused the violation.
		// Display_printf(dispHandle, SPAM_ROW_STATUS_1, 0, "FC
		// Violated: %d", pMsg->msg.flowCtrlEvt.opcode);
	} else if (pMsg->method == ATT_MTU_UPDATED_EVENT) {
		// MTU size updated
		// Display_printf(dispHandle, SPAM_ROW_STATUS_1, 0, "MTU Size:
		// %d", pMsg->msg.mtuEvt.MTU);
	}

	// Free message payload. Needed only for ATT Protocol messages
	GATT_bm_free(&pMsg->msg, pMsg->method);

	// It's safe to free the incoming message
	return (TRUE);
}

/*********************************************************************
 * @fn      BleSpam_processAppMsg
 *
 * @brief   Process an incoming callback from a profile.
 *
 * @param   pMsg - message to process
 *
 * @return  None.
 */
static void BleSpam_processAppMsg(spamEvt_t *pMsg)
{
	if (pMsg->event <= APP_EVT_EVENT_MAX) {
		BLE_LOG_INT_STR(0, BLE_LOG_MODULE_APP,
				"APP : App msg status=%d, event=%s\n", 0,
				appEventStrings[pMsg->event]);
	} else {
		BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP,
				"APP : App msg status=%d, event=0x%x\n", 0,
				pMsg->event);
	}

	switch (pMsg->event) {
	case SPAM_CHAR_CHANGE_EVT:
		BleSpam_processCharValueChangeEvt(*(uint8_t *)(pMsg->pData));
		break;

	case SPAM_UART_CMD_EVT:
		// Drain every queued line: bursts may enqueue more than one
		// line before this event runs, and events could otherwise
		// coalesce.
		while (spamQHead != spamQTail) {
			BleSpam_handleCommand(spamCmdQ[spamQHead]);
			spamQHead = (spamQHead + 1) % SPAM_CMD_QUEUE_N;
		}
		break;

	case SPAM_ADV_EVT:
		BleSpam_processAdvEvent((spamGapAdvEventData_t *)(pMsg->pData));
		break;

	case SPAM_PAIR_STATE_EVT:
		BleSpam_processPairState((spamPairStateData_t *)(pMsg->pData));
		break;

	case SPAM_PASSCODE_EVT:
		BleSpam_processPasscode((spamPasscodeData_t *)(pMsg->pData));
		break;

	case SPAM_CYCLE_EVT:
		BleSpam_processSpamCycle();
		break;

	default:
		// Do nothing.
		break;
	}

	// Free message data if it exists
	if (pMsg->pData != NULL) {
		ICall_free(pMsg->pData);
	}
}

/*********************************************************************
 * @fn      BleSpam_processGapMessage
 *
 * @brief   Process an incoming GAP event.
 *
 * @param   pMsg - message to process
 */
static void BleSpam_processGapMessage(gapEventHdr_t *pMsg)
{
	switch (pMsg->opcode) {
	case GAP_DEVICE_INIT_DONE_EVENT: {
		bStatus_t status = FAILURE;

		gapDeviceInitDoneEvent_t *pPkt =
			(gapDeviceInitDoneEvent_t *)pMsg;

		if (pPkt->hdr.status == SUCCESS) {
			// C4 (D-M1): the stock example copies the (rotating)
			// device address into DEVINFO_SYSTEM_ID, leaking it to
			// any GATT reader. Publish a neutral all-zero System ID
			// instead so the rotating MAC is never exposed.
			uint8_t systemId[DEVINFO_SYSTEM_ID_LEN] = { 0 };

			// Set Device Info Service Parameter
			DevInfo_SetParameter(DEVINFO_SYSTEM_ID,
					     DEVINFO_SYSTEM_ID_LEN, systemId);

			// Display_printf(dispHandle, SPAM_ROW_STATUS_1, 0,
			// "Initialized");

			BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP,
					 "APP : ---- got "
					 "GAP_DEVICE_INIT_DONE_EVENT",
					 0);
			// Setup and start Advertising
			// For more information, see the GAP section in the
			// User's Guide:
			// http://software-dl.ti.com/lprf/ble5stack-latest/
			BleSpam_logAddr("SPAM: init addr", pPkt->devAddr);

			// Spam parameters: ADV_IND (connectable + legacy +
			// scannable), 20-30 ms interval (units of 0.625 ms),
			// all three advertising channels, no white list
			advParams1.eventProps = GAP_ADV_PROP_CONNECTABLE |
						GAP_ADV_PROP_LEGACY |
						GAP_ADV_PROP_SCANNABLE;
			advParams1.primIntMin = 0x20;
			advParams1.primIntMax = 0x30;
			advParams1.primChanMap = GAP_ADV_CHAN_ALL;
			advParams1.filterPolicy = GAP_ADV_WL_POLICY_ANY_REQ;
			advParams1.txPower = 0;
			BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP,
					"APP : ---- call GapAdv_create "
					"set=%d,%d\n",
					0, 0);
			// Create Advertisement set #1 and assign handle
			status = GapAdv_create(&BleSpam_advCallback,
					       &advParams1, &advHandleLegacy);
			if (status != SUCCESS) {
				BleSpam_logErr("adv create", status);
			}
			BLESPAM_ASSERT(status == SUCCESS);

			// Empty scan response, matching the reference
			// implementation (ADV_TYPE_IND without scan response
			// data): the set is scannable but returns no extra
			// bytes, so the only radiated data is the spam model
			// payload loaded by BleSpam_start(). Loading a
			// non-empty scanResData1 here would leak the TI device
			// name into SCAN_RSP and diverge from the reference.
			status = GapAdv_loadByHandle(advHandleLegacy,
						     GAP_ADV_DATA_TYPE_SCAN_RSP,
						     0, NULL);
			if (status != SUCCESS) {
				BleSpam_logErr("scan rsp load", status);
			}

			// Set event mask for set #1
			status = GapAdv_setEventMask(
				advHandleLegacy,
				GAP_ADV_EVT_MASK_START_AFTER_ENABLE |
					GAP_ADV_EVT_MASK_END_AFTER_DISABLE |
					GAP_ADV_EVT_MASK_SET_TERMINATED);
			BLESPAM_ASSERT(status == SUCCESS);

			// Ready: start the engine (first payload + clock)
			spamAdvReady = true;
			BleSpam_start(spamMode);
		}

		break;
	}

	case GAP_LINK_ESTABLISHED_EVENT: {
		gapEstLinkReqEvent_t *pPkt = (gapEstLinkReqEvent_t *)pMsg;

		BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP,
				 "APP : ---- got GAP_LINK_ESTABLISHED_EVENT",
				 0);
		if (pPkt->hdr.status == SUCCESS) {
			bStatus_t termStatus;

			// Spam does not serve connections: terminate the
			// incoming link immediately (D8 reason code)
			termStatus = GAP_TerminateLinkReq(
				pPkt->connectionHandle,
				HCI_DISCONNECT_REMOTE_USER_TERM);
			if (termStatus != SUCCESS) {
				BleSpam_logErr("terminate link", termStatus);
			}
		}
		break;
	}

	case GAP_LINK_TERMINATED_EVENT: {
		BLE_LOG_INT_STR(0, BLE_LOG_MODULE_APP,
				"APP : GAP msg: status=%d, opcode=%s\n", 0,
				"GAP_LINK_TERMINATED_EVENT");
		// Resume the spam engine now that the link is gone
		if (spamRunning) {
			bStatus_t advStatus = GapAdv_enable(
				advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX,
				0);

			if ((advStatus != SUCCESS) &&
			    (advStatus != bleAlreadyInRequestedMode)) {
				BleSpam_logErr("adv re-enable", advStatus);
			}
		}

		break;
	}

	case GAP_UPDATE_LINK_PARAM_REQ_EVENT: {
		gapUpdateLinkParamReqReply_t rsp;

		gapUpdateLinkParamReqEvent_t *pReq =
			(gapUpdateLinkParamReqEvent_t *)pMsg;

		rsp.connectionHandle = pReq->req.connectionHandle;
		rsp.signalIdentifier = pReq->req.signalIdentifier;

		// Only accept connection intervals with slave latency of 0
		// This is just an example of how the application can send a
		// response
		if (pReq->req.connLatency == 0) {
			rsp.intervalMin = pReq->req.intervalMin;
			rsp.intervalMax = pReq->req.intervalMax;
			rsp.connLatency = pReq->req.connLatency;
			rsp.connTimeout = pReq->req.connTimeout;
			rsp.accepted = TRUE;
		} else {
			rsp.accepted = FALSE;
		}

		// Send Reply
		VOID GAP_UpdateLinkParamReqReply(&rsp);

		break;
	}

	default:

		break;
	}
}

/*********************************************************************
 * @fn      BleSpam_charValueChangeCB
 *
 * @brief   Callback from Simple Profile indicating a characteristic
 *          value change.
 *
 * @param   paramId - parameter Id of the value that was changed.
 *
 * @return  None.
 */
static void BleSpam_charValueChangeCB(uint8_t paramId)
{
	uint8_t *pValue = ICall_malloc(sizeof(uint8_t));

	if (pValue) {
		*pValue = paramId;

		if (BleSpam_enqueueMsg(SPAM_CHAR_CHANGE_EVT, pValue) !=
		    SUCCESS) {
			ICall_free(pValue);
		}
	}
}

/*********************************************************************
 * @fn      BleSpam_processCharValueChangeEvt
 *
 * @brief   Process a pending Simple Profile characteristic value change
 *          event.
 *
 * @param   paramID - parameter ID of the value that was changed.
 */
static void BleSpam_processCharValueChangeEvt(uint8_t paramId)
{
	uint8_t newValue;

	switch (paramId) {
	case SIMPLEPROFILE_CHAR1:
		SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR1, &newValue);

		// Display_printf(dispHandle, SPAM_ROW_STATUS_1, 0, "Char 1:
		// %d", (uint16_t)newValue);
		break;

	case SIMPLEPROFILE_CHAR3:
		SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR3, &newValue);

		// Display_printf(dispHandle, SPAM_ROW_STATUS_1, 0, "Char 3:
		// %d", (uint16_t)newValue);
		break;

	default:
		// should not reach here!
		break;
	}
}

/*********************************************************************
 * @fn      BleSpam_clockHandler
 *
 * @brief   Handler function for clock timeouts. Only the spam payload
 *          cycle uses a clock in this application.
 *
 * @param   arg - event type
 *
 * @return  None.
 */
static void BleSpam_clockHandler(UArg arg)
{
	spamClockEventData_t *pData = (spamClockEventData_t *)arg;

	if (pData->event == SPAM_CYCLE_EVT) {
		// Post the cycle to the app queue; when the queue is
		// full, rearm the clock directly so no cycle is lost
		if (BleSpam_enqueueMsg(SPAM_CYCLE_EVT, NULL) != SUCCESS) {
			Util_startClock(&clkSpamCycle);
		}
	}
}

/*********************************************************************
 * @fn      BleSpam_advCallback
 *
 * @brief   GapAdv module callback
 *
 * @param   pMsg - message to process
 */
static void BleSpam_advCallback(uint32_t event, void *pBuf, uintptr_t arg)
{
	spamGapAdvEventData_t *pData =
		ICall_malloc(sizeof(spamGapAdvEventData_t));

	if (pData) {
		pData->event = event;
		pData->pBuf = pBuf;

		if (BleSpam_enqueueMsg(SPAM_ADV_EVT, pData) != SUCCESS) {
			ICall_free(pData);
		}
	}
}

/*********************************************************************
 * @fn      BleSpam_processAdvEvent
 *
 * @brief   Process advertising event in app context
 *
 * @param   pEventData
 */
static void BleSpam_processAdvEvent(spamGapAdvEventData_t *pEventData)
{
	switch (pEventData->event) {
	case GAP_EVT_ADV_START_AFTER_ENABLE:
		BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP,
				 "APP : ---- GAP_EVT_ADV_START_AFTER_ENABLE",
				 0);
		// Display_printf(dispHandle, SPAM_ROW_ADVSTATE, 0, "Adv Set %d
		// Enabled",
		//  *(uint8_t *)(pEventData->pBuf));
		break;

	case GAP_EVT_ADV_END_AFTER_DISABLE:
		// Display_printf(dispHandle, SPAM_ROW_ADVSTATE, 0, "Adv Set %d
		// Disabled",
		//  *(uint8_t *)(pEventData->pBuf));
		break;

	case GAP_EVT_ADV_START:
		break;

	case GAP_EVT_ADV_END:
		break;

	case GAP_EVT_ADV_SET_TERMINATED: {
#ifndef Display_DISABLE_ALL
		GapAdv_setTerm_t *advSetTerm =
			(GapAdv_setTerm_t *)(pEventData->pBuf);

		// Display_printf(dispHandle, SPAM_ROW_ADVSTATE, 0, "Adv Set %d
		// disabled after conn %d", advSetTerm->handle,
		// advSetTerm->connHandle );
		(void)advSetTerm;
#endif
		// The set stopped: re-enable it when no link is active
		// (implicit one-cycle backoff, task 6)
		if (spamRunning && (linkDB_NumActive() == 0)) {
			bStatus_t advStatus = GapAdv_enable(
				advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX,
				0);

			if ((advStatus != SUCCESS) &&
			    (advStatus != bleAlreadyInRequestedMode)) {
				BleSpam_logErr("adv re-enable", advStatus);
			}
		}
	} break;

	case GAP_EVT_SCAN_REQ_RECEIVED:
		break;

	case GAP_EVT_INSUFFICIENT_MEMORY:
		BleSpam_log("ERR: adv insuff mem");
		break;

	default:
		break;
	}

	// All events have associated memory to free except the insufficient
	// memory event
	if (pEventData->event != GAP_EVT_INSUFFICIENT_MEMORY) {
		ICall_free(pEventData->pBuf);
	}
}

/*********************************************************************
 * @fn      BleSpam_pairStateCb
 *
 * @brief   Pairing state callback.
 *
 * @return  none
 */
static void BleSpam_pairStateCb(uint16_t connHandle, uint8_t state,
				uint8_t status)
{
	spamPairStateData_t *pData = ICall_malloc(sizeof(spamPairStateData_t));

	// Allocate space for the event data.
	if (pData) {
		pData->state = state;
		pData->connHandle = connHandle;
		pData->status = status;

		// Queue the event.
		if (BleSpam_enqueueMsg(SPAM_PAIR_STATE_EVT, pData) != SUCCESS) {
			ICall_free(pData);
		}
	}
}

/*********************************************************************
 * @fn      BleSpam_passcodeCb
 *
 * @brief   Passcode callback.
 *
 * @return  none
 */
static void BleSpam_passcodeCb(uint8_t *pDeviceAddr, uint16_t connHandle,
			       uint8_t uiInputs, uint8_t uiOutputs,
			       uint32_t numComparison)
{
	spamPasscodeData_t *pData = ICall_malloc(sizeof(spamPasscodeData_t));

	// Allocate space for the passcode event.
	if (pData) {
		pData->connHandle = connHandle;
		memcpy(pData->deviceAddr, pDeviceAddr, B_ADDR_LEN);
		pData->uiInputs = uiInputs;
		pData->uiOutputs = uiOutputs;
		pData->numComparison = numComparison;

		// Enqueue the event.
		if (BleSpam_enqueueMsg(SPAM_PASSCODE_EVT, pData) != SUCCESS) {
			ICall_free(pData);
		}
	}
}

/*********************************************************************
 * @fn      BleSpam_processPairState
 *
 * @brief   Process the new paring state.
 *
 * @return  none
 */
static void BleSpam_processPairState(spamPairStateData_t *pPairData)
{
	uint8_t state = pPairData->state;
	uint8_t status = pPairData->status;

	switch (state) {
	case GAPBOND_PAIRING_STATE_STARTED:
		// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0, "Pairing
		// started");
		break;

	case GAPBOND_PAIRING_STATE_COMPLETE:
		if (status == SUCCESS) {
			// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0,
			// "Pairing success");
		} else {
			// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0,
			// "Pairing fail: %d", status);
		}
		break;

	case GAPBOND_PAIRING_STATE_ENCRYPTED:
		if (status == SUCCESS) {
			// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0,
			// "Encryption success");
		} else {
			// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0,
			// "Encryption failed: %d", status);
		}
		break;

	case GAPBOND_PAIRING_STATE_BOND_SAVED:
		if (status == SUCCESS) {
			// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0,
			// "Bond save success");
		} else {
			// Display_printf(dispHandle, SPAM_ROW_CONNECTION, 0,
			// "Bond save failed: %d", status);
		}
		break;

	default:
		break;
	}
}

/*********************************************************************
 * @fn      BleSpam_processPasscode
 *
 * @brief   Process the Passcode request.
 *
 * @return  none
 */
static void BleSpam_processPasscode(spamPasscodeData_t *pPasscodeData)
{
	// C3 (D-M2, conservative): the spam engine never serves real
	// connections, so pairing must not succeed with a hard-coded passcode.
	// Reject every passcode request instead of answering with
	// B_APP_DEFAULT_PASSCODE. The aggressive option (USE_PROFILES := 0,
	// dropping SimpleProfile/DevInfo) is deferred to Fase 6 after
	// validating the stack link (R2).
	GAPBondMgr_PasscodeRsp(pPasscodeData->connHandle, FAILURE, 0);
}

/*********************************************************************
 * @fn      BleSpam_enqueueMsg
 *
 * @brief   Creates a message and puts the message in RTOS queue.
 *
 * @param   event - message event.
 * @param   state - message state.
 */
static status_t BleSpam_enqueueMsg(uint8_t event, void *pData)
{
	uint8_t success;
	spamEvt_t *pMsg = ICall_malloc(sizeof(spamEvt_t));

	// Create dynamic pointer to message.
	if (pMsg) {
		pMsg->event = event;
		pMsg->pData = pData;

		// Enqueue the message.
		success = Util_enqueueMsg(appMsgQueueHandle, syncEvent,
					  (uint8_t *)pMsg);
		return (success) ? SUCCESS : FAILURE;
	}

	return (bleMemAllocError);
}

/*********************************************************************
*********************************************************************/
