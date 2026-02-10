/******************************************************************************
 * justworks_scanner.c  —  BLE "Just Works" detector + basic rename probe
 * Target: CC13x2/CC26x2, SimpleLink SDK 8.31 BLE5-Stack (TIRTOS7)
 *
 * - Periodic re-scan (30s by default)
 * - Scan FSM + watchdog for stuck transitions
 * - Connects to connectable advertisers (per-MAC attempt limit & backoff)
 * - Detects "Just Works" pairing (no user interaction)
 * - After connect (paired or not): discover GAP service → read Device Name → try to write new name
 * - UART logging with rate limiting per MAC
 ******************************************************************************/

#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RTOS */
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Queue.h>

/* TI Drivers / Utils */
#include <ti/display/Display.h>
#include <icall.h>
#include "util.h"

/* BLE Stack */
#include "bcomdef.h"
#include "icall_ble_api.h"
#include "osal_list.h"
#include "gap.h"
#include "gatt.h"
#include "att.h"
#include "gap_scanner.h"
#include "gap_initiator.h"

/* Board / Config */
#include <ti_drivers_config.h>
#include "ti_ble_config.h"
#include "ble_user_config.h"

/* ========================= Helpers/Macros ================== */

#ifndef DISPLAY_ROW_UNUSED
#define DISPLAY_ROW_UNUSED 0xFF
#endif

/* Visible per your note */
Display_Handle dispHandle = NULL;

#define DPRINTF(...) \
	Display_printf(dispHandle, DISPLAY_ROW_UNUSED, 0, __VA_ARGS__)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* ========================= App Events ====================== */

#define JW_EVT_SCAN_ENABLED 0x01
#define JW_EVT_SCAN_DISABLED 0x02
#define JW_EVT_ADV_REPORT 0x03
#define JW_EVT_PAIR_STATE 0x04
#define JW_EVT_PASSCODE_NEEDED 0x05
#define JW_EVT_READ_RPA 0x06
#define JW_EVT_INSUFFICIENT_MEM 0x07
#define JW_EVT_SCAN_RESTART 0x08
#define JW_EVT_SCAN_WATCHDOG 0x09

/* ========================= General ========================= */

#define JW_ICALL_EVT ICALL_MSG_EVENT_ID
#define JW_QUEUE_EVT UTIL_QUEUE_EVENT_ID
#define JW_ALL_EVENTS (JW_ICALL_EVT | JW_QUEUE_EVT)

#define JW_TASK_PRIORITY 1
#ifndef JW_TASK_STACK_SIZE
#define JW_TASK_STACK_SIZE 1024
#endif

#define CONN_TIMEOUT_MS_CONVERSION 10
#define CONNECTION_TIMEOUT 3000

/* ========================= Scan / Backoff ================== */

#define SCAN_RESTART_PERIOD 30000	/* ms */
#define SCAN_WATCHDOG_PERIOD 3000	/* ms */
#define SCAN_TRANSITION_TIMEOUT_MS 2500 /* ms */

/* Backoff to avoid duplicate connect initiations to the same address */
#define CONNECT_RETRY_BACKOFF_MS 8000

/* Max connect attempts per MAC before we stop testing that device entirely */
#ifndef MAX_ATTEMPTS_PER_DEVICE
#define MAX_ATTEMPTS_PER_DEVICE 3
#endif

/* Per-MAC log rate limit (adv spam control) */
#define LOG_RATELIMIT_MS 1500

/* Unique report-field selection to avoid redefining ADV_RPT_FIELDS from SysConfig */
#define JW_ADV_RPT_FIELDS                                      \
	(SCAN_ADVRPT_FLD_EVENTTYPE | SCAN_ADVRPT_FLD_ADDRESS | \
	 SCAN_ADVRPT_FLD_ADDRTYPE | SCAN_ADVRPT_FLD_PRIMPHY |  \
	 SCAN_ADVRPT_FLD_SECPHY | SCAN_ADVRPT_FLD_DATALEN)

#define JW_ASSERT(expr) \
	if (!(expr))    \
	JustWorksScanner_spin()

/* ========================= Types =========================== */

typedef struct {
	appEvtHdr_t hdr;
	uint8_t *pData;
} jwEvt_t;

typedef struct {
	uint16_t connHandle;
	uint16_t charHandle;
	uint8_t addr[B_ADDR_LEN];
} connRec_t;

typedef struct {
	uint16_t connHandle;
	uint8_t status;
} jwPairStateData_t;

typedef struct {
	uint8_t deviceAddr[B_ADDR_LEN];
	uint16_t connHandle;
	uint8_t uiInputs;
	uint8_t uiOutputs;
	uint32_t numComparison;
} jwPasscodeData_t;

/* Scan FSM */
typedef enum {
	SCAN_STATE_IDLE = 0,
	SCAN_STATE_ENABLING,
	SCAN_STATE_ENABLED,
	SCAN_STATE_DISABLING,
} scan_state_t;

/* GAP name discovery / write state per connection */
typedef enum {
	DISC_NONE = 0,
	DISC_FIND_GAP_SERVICE, /* Discover primary service 0x1800 (GAP) */
	DISC_READ_DEVNAME, /* Read Device Name (0x2A00) using ReadUsingCharUUID */
	DISC_WRITE_DEVNAME, /* Try to write new Device Name */
	DISC_DONE
} disc_step_t;

typedef struct {
	uint8_t inUse;
	uint16_t connHandle;
	uint16_t gapStartHdl, gapEndHdl;
	uint16_t devNameValHdl; /* Value handle for Device Name (from ReadUsingCharUUID) */
	disc_step_t step;
	char prevName[32];
} nameDisc_t;

/* ========================= Globals ========================= */

/* ICall */
static ICall_EntityID selfEntity;
static ICall_SyncHandle syncEvent;

/* Queue */
static Queue_Struct appMsg;
static Queue_Handle appMsgQueue;

/* Task */
static Task_Struct jwTask;
#if defined __TI_COMPILER_VERSION__
#pragma DATA_ALIGN(jwTaskStack, 8)
#else
#pragma data_alignment = 8
#endif
static uint8_t jwTaskStack[JW_TASK_STACK_SIZE];

/* Connections */
static uint8_t numConn = 0;
static connRec_t connList[MAX_NUM_BLE_CONNS];

/* GATT / Addr */
static GAP_Addr_Modes_t addrMode = DEFAULT_ADDRESS_MODE;
static uint8 rpa[B_ADDR_LEN] = { 0 };

/* Clocks */
static Clock_Struct clkRpaRead;
static Clock_Struct clkScanRestart;
static Clock_Struct clkScanWatchdog;

/* Scan FSM state */
static volatile scan_state_t scanState = SCAN_STATE_IDLE;
static volatile bool wantRestart = false;
static uint32_t lastScanTransitionMs = 0;

/* Name + RSSI cache (from ADV) + log rate limiting */
typedef struct {
	uint8_t addr[B_ADDR_LEN];
	char name[31];
	int8 lastRssi;
	uint32_t lastLogMs;
} nameCache_t;
#define NAME_CACHE_MAX 48
static nameCache_t nameCache[NAME_CACHE_MAX];

/* Per-MAC connect throttle */
typedef struct {
	uint8_t addr[B_ADDR_LEN];
	uint32_t tick;
	uint8_t attempts;
	uint8_t exhausted;
} connectTry_t;
#define CONNECT_TRY_MAX 48
static connectTry_t connectTries[CONNECT_TRY_MAX];

/* “Saw passcode prompt” per connection (for JW inference) */
static bool sawPasscodePrompt[MAX_NUM_BLE_CONNS];

/* Name discovery contexts */
static nameDisc_t nameCtx[MAX_NUM_BLE_CONNS];

/* ========================= Forward Decls =================== */

static void JustWorksScanner_init(void);
static void JustWorksScanner_taskFxn(uintptr_t a0, uintptr_t a1);

static uint8_t JustWorksScanner_processStackMsg(ICall_Hdr *pMsg);
static void JustWorksScanner_processGapMsg(gapEventHdr_t *pMsg);
static void JustWorksScanner_processGATTMsg(gattMsgEvent_t *pMsg);

static void JustWorksScanner_processAppMsg(jwEvt_t *pMsg);

static uint8_t JustWorksScanner_addConnInfo(uint16_t connHandle,
					    uint8_t *pAddr);
static uint8_t JustWorksScanner_removeConnInfo(uint16_t connHandle);
static uint8_t JustWorksScanner_getConnIndex(uint16_t connHandle);
#ifndef Display_DISABLE_ALL
static char *JustWorksScanner_getConnAddrStr(uint16_t connHandle);
#endif

static void JustWorksScanner_spin(void);
static void JustWorksScanner_clockHandler(UArg arg);
static void JustWorksScanner_scanCb(uint32_t evt, void *pMsg, uintptr_t arg);

static void setBondManagerParameters_ForJustWorks(void);
static void JustWorksScanner_logInsecureJustWorks(uint16_t connHandle,
						  const char *reason);

static const char *addrTypeStr(uint8_t t);
static const char *phyToStr(uint8_t phy);
static const char *evtTypeToStr(uint16_t evtType, char *buf, size_t n);
static const char *discReasonToStr(uint8_t r);
static const char *pairStatusToStr(uint8_t s);

/* BondMgr callbacks */
static void JustWorksScanner_passcodeCb(uint8_t *deviceAddr,
					uint16_t connHandle, uint8_t uiInputs,
					uint8_t uiOutputs,
					uint32_t numComparison);
static void JustWorksScanner_pairStateCb(uint16_t connHandle, uint8_t state,
					 uint8_t status);

/* Device name discovery / write */
static void startDevNameDiscovery(uint16_t connHandle);
static void handleReadByGrpTypeRsp(uint16_t connHandle,
				   const attReadByGrpTypeRsp_t *rsp);
static void handleReadByTypeRsp_Name(uint16_t connHandle,
				     const attReadByTypeRsp_t *rsp);
static void continueNameFlow(uint16_t connHandle);

/* ========================= Small helpers =================== */

static bool isZeroBuf(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i])
			return false;
	}
	return true;
}

static uint32_t msTicks(void)
{
	uint32_t t = Clock_getTicks();
#if defined(Clock_tickPeriod)
	return (t * Clock_tickPeriod) / 1000;
#else
	return t;
#endif
}

static void cacheNameRssi(uint8_t addr[B_ADDR_LEN], const char *name, int8 rssi)
{
	uint8_t i, freeIdx = 0xFF;
	for (i = 0; i < NAME_CACHE_MAX; i++) {
		if (memcmp(nameCache[i].addr, addr, B_ADDR_LEN) == 0) {
			if (name) {
				strncpy(nameCache[i].name, name,
					sizeof(nameCache[i].name) - 1);
				nameCache[i]
					.name[sizeof(nameCache[i].name) - 1] =
					0;
			}
			nameCache[i].lastRssi = rssi;
			return;
		}
		if (freeIdx == 0xFF && isZeroBuf(nameCache[i].addr, B_ADDR_LEN))
			freeIdx = i;
	}
	if (freeIdx != 0xFF) {
		memcpy(nameCache[freeIdx].addr, addr, B_ADDR_LEN);
		nameCache[freeIdx].lastRssi = rssi;
		if (name) {
			strncpy(nameCache[freeIdx].name, name,
				sizeof(nameCache[freeIdx].name) - 1);
			nameCache[freeIdx]
				.name[sizeof(nameCache[freeIdx].name) - 1] = 0;
		} else
			nameCache[freeIdx].name[0] = 0;
		nameCache[freeIdx].lastLogMs = 0;
	}
}
static const char *lookupName(uint8_t addr[B_ADDR_LEN])
{
	for (uint8_t i = 0; i < NAME_CACHE_MAX; i++)
		if (memcmp(nameCache[i].addr, addr, B_ADDR_LEN) == 0)
			return nameCache[i].name[0] ? nameCache[i].name :
						      "(no-name)";
	return "(no-name)";
}
static int8 lookupRssi(uint8_t addr[B_ADDR_LEN])
{
	for (uint8_t i = 0; i < NAME_CACHE_MAX; i++)
		if (memcmp(nameCache[i].addr, addr, B_ADDR_LEN) == 0)
			return nameCache[i].lastRssi;
	return 0;
}
static bool logAllowed(uint8_t addr[B_ADDR_LEN])
{
	uint32_t now = msTicks();
	for (uint8_t i = 0; i < NAME_CACHE_MAX; i++) {
		if (memcmp(nameCache[i].addr, addr, B_ADDR_LEN) == 0) {
			if (now - nameCache[i].lastLogMs >= LOG_RATELIMIT_MS) {
				nameCache[i].lastLogMs = now;
				return true;
			}
			return false;
		}
	}
	return true;
}

/* Connect attempt limiter + backoff */
static bool connectTry_allowedAndMark(uint8_t addr[B_ADDR_LEN])
{
	uint32_t now = msTicks();
	int freeIdx = -1, reuseIdx = -1;

	for (int i = 0; i < CONNECT_TRY_MAX; i++) {
		if (isZeroBuf(connectTries[i].addr, B_ADDR_LEN)) {
			if (freeIdx < 0)
				freeIdx = i;
			continue;
		}
		if (memcmp(connectTries[i].addr, addr, B_ADDR_LEN) == 0) {
			if (connectTries[i].exhausted)
				return false;
			if ((now - connectTries[i].tick) <
			    CONNECT_RETRY_BACKOFF_MS)
				return false;
			reuseIdx = i;
			break;
		}
	}

	int idx = (reuseIdx >= 0) ? reuseIdx : (freeIdx >= 0 ? freeIdx : 0);
	memcpy(connectTries[idx].addr, addr, B_ADDR_LEN);
	connectTries[idx].tick = now;
	if (connectTries[idx].attempts < 0xFF)
		connectTries[idx].attempts++;
	if (connectTries[idx].attempts >= MAX_ATTEMPTS_PER_DEVICE) {
		connectTries[idx].exhausted = 1;
		return false;
	}
	return true;
}
static void connectTry_markExhausted(uint8_t addr[B_ADDR_LEN])
{
	for (int i = 0; i < CONNECT_TRY_MAX; i++) {
		if (memcmp(connectTries[i].addr, addr, B_ADDR_LEN) == 0) {
			connectTries[i].exhausted = 1;
			return;
		}
	}
}

/* Parse Complete/Shortened Local Name from adv payload */
static uint8_t parseLocalName(uint8_t *pData, uint16_t len, char *out,
			      uint8_t outSz)
{
	uint8_t *p = pData, *end = pData + len;
	while (p + 1 < end) {
		uint8_t adLen = *p++;
		if (adLen == 0 || p + adLen > end)
			break;
		uint8_t adType = *p++;
		uint8_t fieldLen = adLen - 1;
		if (adType == GAP_ADTYPE_LOCAL_NAME_COMPLETE ||
		    adType == GAP_ADTYPE_LOCAL_NAME_SHORT) {
			uint8_t cpy = (fieldLen < (outSz - 1)) ? fieldLen :
								 (outSz - 1);
			memcpy(out, p, cpy);
			out[cpy] = 0;
			return 1;
		}
		p += fieldLen;
	}
	return 0;
}

/* ========================= Public ========================== */

void JustWorksScanner_createTask(void)
{
	Task_Params taskParams;
	Task_Params_init(&taskParams);
	taskParams.stack = jwTaskStack;
	taskParams.stackSize = JW_TASK_STACK_SIZE;
	taskParams.priority = JW_TASK_PRIORITY;
	Task_construct(&jwTask, JustWorksScanner_taskFxn, &taskParams, NULL);
}

/* ========================= Init/Task ======================= */

static void JustWorksScanner_spin(void)
{
	volatile uint8_t x = 0;
	while (1) {
		x++;
	}
}

static void JustWorksScanner_init(void)
{
	ICall_registerApp(&selfEntity, &syncEvent);
	appMsgQueue = Util_constructQueue(&appMsg);

	memset(connList, 0, sizeof(connList));
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
		connList[i].connHandle = LINKDB_CONNHANDLE_INVALID;
	memset(nameCache, 0, sizeof(nameCache));
	memset(connectTries, 0, sizeof(connectTries));
	memset(sawPasscodePrompt, 0, sizeof(sawPasscodePrompt));
	memset(nameCtx, 0, sizeof(nameCtx));

	/* Friendly device name (from SysConfig attDeviceName) */
	GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN,
			 (void *)attDeviceName);

	/* GATT/GAP services */
	VOID GATT_InitClient();
	GATT_RegisterForInd(selfEntity);
	GGS_AddService(GAP_SERVICE);
	GATTServApp_AddService(GATT_ALL_SERVICES);
	GATT_RegisterForMsgs(selfEntity);

	/* Bond Manager: Just Works, central-initiated */
	setBondManagerParameters_ForJustWorks();
	static gapBondCBs_t bondMgrCBs = { JustWorksScanner_passcodeCb,
					   JustWorksScanner_pairStateCb };
	VOID GAPBondMgr_Register(&bondMgrCBs);

	GAP_SetParamValue(GAP_PARAM_LINK_UPDATE_DECISION,
			  GAP_UPDATE_REQ_ACCEPT_ALL);
	GAP_RegisterForMsgs(selfEntity);

	/* Initialize as Central */
	GAP_DeviceInit(GAP_PROFILE_CENTRAL, selfEntity, addrMode,
		       &pRandomAddress);

	/* UART Display */
	dispHandle = Display_open(Display_Type_UART, NULL);
	DPRINTF("===== JustWorks Scanner (UART) =====");
	DPRINTF("Auto scan + 30s relaunch...");
}

static void JustWorksScanner_taskFxn(uintptr_t a0, uintptr_t a1)
{
	JustWorksScanner_init();

	for (;;) {
		uint32_t events = Event_pend(syncEvent, Event_Id_NONE,
					     JW_ALL_EVENTS,
					     ICALL_TIMEOUT_FOREVER);

		if (events) {
			ICall_EntityID dest;
			ICall_ServiceEnum src;
			ICall_HciExtEvt *pMsg = NULL;
			if (ICall_fetchServiceMsg(&src, &dest,
						  (void **)&pMsg) ==
			    ICALL_ERRNO_SUCCESS) {
				uint8 safeToDealloc = TRUE;
				if ((src == ICALL_SERVICE_CLASS_BLE) &&
				    (dest == selfEntity)) {
					ICall_Stack_Event *pEvt =
						(ICall_Stack_Event *)pMsg;
					if (pEvt->signature != 0xFFFF)
						safeToDealloc =
							JustWorksScanner_processStackMsg(
								(ICall_Hdr *)
									pMsg);
				}
				if (pMsg && safeToDealloc)
					ICall_freeMsg(pMsg);
			}

			if (events & JW_QUEUE_EVT) {
				jwEvt_t *pMsg;
				while ((pMsg = (jwEvt_t *)Util_dequeueMsg(
						appMsgQueue))) {
					JustWorksScanner_processAppMsg(pMsg);
					ICall_free(pMsg);
				}
			}
		}
	}
}

/* ========================= Stack Msgs ====================== */

static uint8_t JustWorksScanner_processStackMsg(ICall_Hdr *pMsg)
{
	switch (pMsg->event) {
	case GAP_MSG_EVENT:
		JustWorksScanner_processGapMsg((gapEventHdr_t *)pMsg);
		break;
	case GATT_MSG_EVENT:
		JustWorksScanner_processGATTMsg((gattMsgEvent_t *)pMsg);
		break;
	case HCI_GAP_EVENT_EVENT:
		if (pMsg->status == HCI_BLE_HARDWARE_ERROR_EVENT_CODE)
			DPRINTF("[DBG] HCI hardware error");
		break;
	default:
		break;
	}
	return TRUE;
}

/* ========================= App Msgs ======================== */

static status_t JustWorksScanner_enqueueMsg(uint8_t event, uint8_t state,
					    uint8_t *pData)
{
	jwEvt_t *pMsg = ICall_malloc(sizeof(jwEvt_t));
	if (pMsg) {
		pMsg->hdr.event = event;
		pMsg->hdr.state = state;
		pMsg->pData = pData;
		if (Util_enqueueMsg(appMsgQueue, syncEvent, (uint8_t *)pMsg))
			return SUCCESS;
		ICall_free(pMsg);
	}
	return bleMemAllocError;
}

static void JustWorksScanner_processAppMsg(jwEvt_t *pMsg)
{
	switch (pMsg->hdr.event) {
	case JW_EVT_ADV_REPORT: {
		GapScan_Evt_AdvRpt_t *pAdv =
			(GapScan_Evt_AdvRpt_t *)(pMsg->pData);
		char name[31] = { 0 };
		parseLocalName(pAdv->pData, pAdv->dataLen, name, sizeof(name));
		cacheNameRssi(pAdv->addr, name[0] ? name : NULL, pAdv->rssi);

		if (logAllowed(pAdv->addr)) {
			char evtStr[64];
			evtTypeToStr(pAdv->evtType, evtStr, sizeof(evtStr));
			DPRINTF("[SCAN] ADV from %s | name:%s | RSSI:%d dBm | addrType:%s | primPHY:%s secPHY:%s evt:%s",
				Util_convertBdAddr2Str(pAdv->addr),
				name[0] ? name : "(no-name)", (int)pAdv->rssi,
				addrTypeStr(pAdv->addrType),
				phyToStr(pAdv->primPhy), phyToStr(pAdv->secPhy),
				evtStr);
		}

		/* Connect to connectable devices (dedupe/backoff/limit) */
		if (numConn < MAX_NUM_BLE_CONNS &&
		    (pAdv->evtType & ADV_RPT_EVT_TYPE_CONNECTABLE)) {
			bool exhausted = false;
			for (int i = 0; i < CONNECT_TRY_MAX; i++) {
				if (memcmp(connectTries[i].addr, pAdv->addr,
					   B_ADDR_LEN) == 0 &&
				    connectTries[i].exhausted) {
					exhausted = true;
					break;
				}
			}
			if (!exhausted &&
			    connectTry_allowedAndMark(pAdv->addr)) {
				DPRINTF("[CONN] Initiating to %s ...",
					Util_convertBdAddr2Str(pAdv->addr));
				GapInit_connect(pAdv->addrType &
							MASK_ADDRTYPE_ID,
						pAdv->addr, DEFAULT_INIT_PHY,
						CONNECTION_TIMEOUT);
			} else if (exhausted) {
				DPRINTF("[CONN] Max attempts reached for %s; skipping further tests",
					Util_convertBdAddr2Str(pAdv->addr));
			}
		}

		if (pAdv->pData)
			ICall_free(pAdv->pData);
	} break;

	case JW_EVT_SCAN_ENABLED:
		scanState = SCAN_STATE_ENABLED;
		lastScanTransitionMs = msTicks();
		DPRINTF("[SCAN] enabled");
		if (wantRestart) {
			wantRestart = false;
			DPRINTF("[SCAN] queued restart → disabling now");
			scanState = SCAN_STATE_DISABLING;
			GapScan_disable();
		}
		break;

	case JW_EVT_SCAN_DISABLED:
		scanState = SCAN_STATE_IDLE;
		lastScanTransitionMs = msTicks();
		DPRINTF("[SCAN] disabled");
		if (wantRestart) {
			wantRestart = false;
			DPRINTF("[SCAN] restarting now");
			scanState = SCAN_STATE_ENABLING;
			GapScan_enable(0, DEFAULT_SCAN_DURATION, 0);
		}
		break;

	case JW_EVT_SCAN_RESTART:
		DPRINTF("[SCAN] restart tick (every %ums)",
			(unsigned)SCAN_RESTART_PERIOD);
		switch (scanState) {
		case SCAN_STATE_IDLE:
			DPRINTF("[SCAN] starting (was idle)");
			scanState = SCAN_STATE_ENABLING;
			lastScanTransitionMs = msTicks();
			GapScan_enable(0, DEFAULT_SCAN_DURATION, 0);
			break;
		case SCAN_STATE_ENABLED:
			DPRINTF("[SCAN] disabling to restart ...");
			wantRestart = true;
			scanState = SCAN_STATE_DISABLING;
			lastScanTransitionMs = msTicks();
			GapScan_disable();
			break;
		case SCAN_STATE_ENABLING:
		case SCAN_STATE_DISABLING:
			wantRestart = true;
			DPRINTF("[SCAN] transition in progress (%s); will restart after it completes",
				scanState == SCAN_STATE_ENABLING ? "ENABLING" :
								   "DISABLING");
			break;
		}
		break;

	case JW_EVT_SCAN_WATCHDOG: {
		uint32_t now = msTicks();
		if ((scanState == SCAN_STATE_ENABLING ||
		     scanState == SCAN_STATE_DISABLING) &&
		    (now - lastScanTransitionMs) > SCAN_TRANSITION_TIMEOUT_MS) {
			DPRINTF("[SCAN][WD] transition timeout in state=%d; forcing disable→enable",
				(int)scanState);
			GapScan_disable(); /* OK even if already disabled */
			wantRestart = false;
			scanState = SCAN_STATE_ENABLING;
			lastScanTransitionMs = now;
			GapScan_enable(0, DEFAULT_SCAN_DURATION, 0);
		}
	} break;

	case JW_EVT_PAIR_STATE: {
		jwPairStateData_t *pPair = (jwPairStateData_t *)pMsg->pData;
		uint8_t status = pPair->status;
		uint16_t ch = pPair->connHandle;
		uint8_t idx = JustWorksScanner_getConnIndex(ch);

		if (pMsg->hdr.state == GAPBOND_PAIRING_STATE_STARTED) {
			DPRINTF("[PAIR] started (conn=0x%04X)", ch);
		} else if (pMsg->hdr.state == GAPBOND_PAIRING_STATE_COMPLETE) {
			if (status == SUCCESS) {
				DPRINTF("[PAIR] success (conn=0x%04X)", ch);
				if (idx < MAX_NUM_BLE_CONNS &&
				    !sawPasscodePrompt[idx])
					JustWorksScanner_logInsecureJustWorks(
						ch,
						"no user interaction during pairing");
			} else {
				DPRINTF("[PAIR] failed (0x%02X %s) conn=0x%04X",
					status, pairStatusToStr(status), ch);
			}
		} else if (pMsg->hdr.state == GAPBOND_PAIRING_STATE_ENCRYPTED) {
			DPRINTF("[PAIR] %s (conn=0x%04X)",
				(status == SUCCESS) ? "link encrypted" :
						      "encryption failed",
				ch);
		} else if (pMsg->hdr.state ==
			   GAPBOND_PAIRING_STATE_BOND_SAVED) {
			DPRINTF("[PAIR] bond %s (conn=0x%04X)",
				(status == SUCCESS) ? "saved" : "save failed",
				ch);
		}
	} break;

	case JW_EVT_PASSCODE_NEEDED: {
		jwPasscodeData_t *pData = (jwPasscodeData_t *)pMsg->pData;
		uint16_t ch = pData->connHandle;
		DPRINTF("[PAIR] PasscodeReq conn=0x%04X uiIn:%u uiOut:%u numCmp:%lu",
			ch, pData->uiInputs, pData->uiOutputs,
			(unsigned long)pData->numComparison);

		uint8_t idx = JustWorksScanner_getConnIndex(ch);
		if (idx < MAX_NUM_BLE_CONNS)
			sawPasscodePrompt[idx] = true;

		if (pData->uiInputs == 0 && pData->uiOutputs == 0 &&
		    pData->numComparison == 0) {
			JustWorksScanner_logInsecureJustWorks(
				ch, "no IO, no Numeric Comparison");
		}

		GAPBondMgr_PasscodeRsp(ch, SUCCESS, B_APP_DEFAULT_PASSCODE);
	} break;

	case JW_EVT_READ_RPA: {
		uint8_t *pRpaNew = GAP_GetDevAddress(FALSE);
		if (memcmp(pRpaNew, rpa, B_ADDR_LEN)) {
			DPRINTF("[DBG] RP Addr changed: %s",
				Util_convertBdAddr2Str(pRpaNew));
			memcpy(rpa, pRpaNew, B_ADDR_LEN);
		}
	} break;

	case JW_EVT_INSUFFICIENT_MEM:
		DPRINTF("[WARN] Insufficient memory; stopping scan");
		GapScan_disable();
		break;

	default:
		break;
	}

	if (pMsg->pData)
		ICall_free(pMsg->pData);
}

/* ========================= GAP Msgs ======================== */

static void JustWorksScanner_processGapMsg(gapEventHdr_t *pMsg)
{
	switch (pMsg->opcode) {
	case GAP_DEVICE_INIT_DONE_EVENT: {
		gapDeviceInitDoneEvent_t *pPkt =
			(gapDeviceInitDoneEvent_t *)pMsg;
		uint8_t temp8;
		uint16_t temp16;

		/* Scanner config */
		GapScan_registerCb(JustWorksScanner_scanCb, NULL);
		GapScan_setEventMask(GAP_EVT_SCAN_ENABLED |
				     GAP_EVT_SCAN_DISABLED |
				     GAP_EVT_ADV_REPORT);
		GapScan_setPhyParams(DEFAULT_SCAN_PHY, DEFAULT_SCAN_TYPE,
				     DEFAULT_SCAN_INTERVAL,
				     DEFAULT_SCAN_WINDOW);

		temp16 = JW_ADV_RPT_FIELDS;
		GapScan_setParam(SCAN_PARAM_RPT_FIELDS, &temp16);
		temp8 = DEFAULT_SCAN_PHY;
		GapScan_setParam(SCAN_PARAM_PRIM_PHYS, &temp8);
		temp8 = SCANNER_DUPLICATE_FILTER;
		GapScan_setParam(SCAN_PARAM_FLT_DUP, &temp8);
		temp16 = SCAN_FLT_PDU_CONNECTABLE_ONLY |
			 SCAN_FLT_PDU_COMPLETE_ONLY;
		GapScan_setParam(SCAN_PARAM_FLT_PDU_TYPE, &temp16);

		/* Initiator params */
		GapInit_setPhyParam(DEFAULT_INIT_PHY,
				    INIT_PHYPARAM_CONN_INT_MIN,
				    INIT_PHYPARAM_MIN_CONN_INT);
		GapInit_setPhyParam(DEFAULT_INIT_PHY,
				    INIT_PHYPARAM_CONN_INT_MAX,
				    INIT_PHYPARAM_MAX_CONN_INT);

		DPRINTF("[INIT] Done. DevAddr: %s",
			Util_convertBdAddr2Str(pPkt->devAddr));

		if (addrMode > ADDRMODE_RANDOM) {
			memcpy(rpa, GAP_GetDevAddress(FALSE), B_ADDR_LEN);
			DPRINTF("[INIT] RP Addr: %s",
				Util_convertBdAddr2Str(rpa));
			Util_constructClock(&clkRpaRead,
					    JustWorksScanner_clockHandler,
					    30000, 0, true, JW_EVT_READ_RPA);
		}

		/* Start initial scan */
		DPRINTF("[SCAN] starting initial scan");
		scanState = SCAN_STATE_ENABLING;
		lastScanTransitionMs = msTicks();
		GapScan_enable(0, DEFAULT_SCAN_DURATION, 0);

		/* Periodic relaunch + watchdog */
		Util_constructClock(&clkScanRestart,
				    JustWorksScanner_clockHandler,
				    SCAN_RESTART_PERIOD, 0, true,
				    JW_EVT_SCAN_RESTART);
		Util_constructClock(&clkScanWatchdog,
				    JustWorksScanner_clockHandler,
				    SCAN_WATCHDOG_PERIOD, 0, true,
				    JW_EVT_SCAN_WATCHDOG);
		DPRINTF("[SCAN] periodic relaunch set to %ums",
			(unsigned)SCAN_RESTART_PERIOD);
	} break;

	case GAP_CONNECTING_CANCELLED_EVENT:
		DPRINTF("[CONN] Connecting attempt cancelled");
		break;

	case GAP_LINK_ESTABLISHED_EVENT: {
		gapEstLinkReqEvent_t *e = (gapEstLinkReqEvent_t *)pMsg;
		uint8_t connIndex = JustWorksScanner_addConnInfo(
			e->connectionHandle, e->devAddr);
		JW_ASSERT(connIndex < MAX_NUM_BLE_CONNS);
		sawPasscodePrompt[connIndex] = false;

		DPRINTF("[CONN] Connected: %s | name:%s | RSSI:%d",
			Util_convertBdAddr2Str(e->devAddr),
			lookupName(e->devAddr), (int)lookupRssi(e->devAddr));

#ifdef GAPBondMgr_Pair
		GAPBondMgr_Pair(e->connectionHandle);
		DPRINTF("[PAIR] Initiated via GAPBondMgr_Pair()");
#else
		GAPBondMgr_SetPairable(TRUE);
		DPRINTF("[PAIR] Pairable set (INITIATE mode should trigger)");
#endif

		/* Kick off GAP Device Name read/rename flow (works with or without pairing) */
		startDevNameDiscovery(e->connectionHandle);
	} break;

	case GAP_LINK_TERMINATED_EVENT: {
		gapTerminateLinkEvent_t *e = (gapTerminateLinkEvent_t *)pMsg;
		uint8_t idx =
			JustWorksScanner_removeConnInfo(e->connectionHandle);
		JW_ASSERT(idx < MAX_NUM_BLE_CONNS);
		DPRINTF("[CONN] Disconnected: %s (reason=0x%02X %s)",
			Util_convertBdAddr2Str(connList[idx].addr), e->reason,
			discReasonToStr(e->reason));
	} break;

	case GAP_LINK_PARAM_UPDATE_EVENT: {
		gapLinkUpdateEvent_t *e = (gapLinkUpdateEvent_t *)pMsg;
		linkDBInfo_t info;
		if (linkDB_GetInfo(e->connectionHandle, &info) == SUCCESS)
			DPRINTF("[CONN] Param update: %s timeout:%dms",
				Util_convertBdAddr2Str(info.addr),
				info.connTimeout * CONN_TIMEOUT_MS_CONVERSION);
	} break;

	default:
		break;
	}
}

/* ========================= GATT Msgs ======================= */

static void JustWorksScanner_processGATTMsg(gattMsgEvent_t *pMsg)
{
	uint16_t ch = pMsg->connHandle;

	if (!linkDB_Up(ch)) {
		GATT_bm_free(&pMsg->msg, pMsg->method);
		return;
	}
	if (pMsg->hdr.status == blePending)
		DPRINTF("[GATT] ATT rsp dropped %d", pMsg->method);
	else if (pMsg->method == ATT_MTU_UPDATED_EVENT)
		DPRINTF("[GATT] MTU updated: %d", pMsg->msg.mtuEvt.MTU);

	nameDisc_t *c = NULL;
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
		if (nameCtx[i].inUse && nameCtx[i].connHandle == ch) {
			c = &nameCtx[i];
			break;
		}

	if (c) {
		switch (pMsg->method) {
		case ATT_READ_BY_GRP_TYPE_RSP:
			handleReadByGrpTypeRsp(ch, &pMsg->msg.readByGrpTypeRsp);
			break;

		case ATT_READ_BY_TYPE_RSP:
			if (c->step == DISC_READ_DEVNAME)
				handleReadByTypeRsp_Name(
					ch, &pMsg->msg.readByTypeRsp);
			break;

		case ATT_WRITE_RSP:
			if (c->step == DISC_WRITE_DEVNAME) {
				DPRINTF("[INFO] Name write OK: \"%s\" -> \"Secure your device\"",
					c->prevName);
				c->step = DISC_DONE;
			}
			break;

		case ATT_ERROR_RSP: {
			attErrorRsp_t *e = &pMsg->msg.errorRsp;
			if (c->step == DISC_READ_DEVNAME) {
				DPRINTF("[INFO] GAP info flow stopped: DevName read failed (err=0x%02X, attr=0x%04X)",
					e->errCode, e->handle);
				c->step = DISC_DONE;
			} else if (c->step == DISC_WRITE_DEVNAME) {
				DPRINTF("[INFO] Name write failed (err=0x%02X)",
					e->errCode);
				c->step = DISC_DONE;
			}
		} break;

		default:
			break;
		}
	}

	GATT_bm_free(&pMsg->msg, pMsg->method);
}

/* ==================== Name Discovery / Write =============== */

static nameDisc_t *allocNameCtx(uint16_t connHandle)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++) {
		if (!nameCtx[i].inUse) {
			memset(&nameCtx[i], 0, sizeof(nameCtx[i]));
			nameCtx[i].inUse = 1;
			nameCtx[i].connHandle = connHandle;
			nameCtx[i].step = DISC_FIND_GAP_SERVICE;
			return &nameCtx[i];
		}
	}
	return NULL;
}

static nameDisc_t *getNameCtx(uint16_t connHandle)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
		if (nameCtx[i].inUse && nameCtx[i].connHandle == connHandle)
			return &nameCtx[i];
	return NULL;
}

static void freeNameCtx(uint16_t connHandle)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
		if (nameCtx[i].inUse && nameCtx[i].connHandle == connHandle) {
			memset(&nameCtx[i], 0, sizeof(nameCtx[i]));
			return;
		}
}

static void startDevNameDiscovery(uint16_t connHandle)
{
	nameDisc_t *c = allocNameCtx(connHandle);
	if (!c) {
		DPRINTF("[INFO] GAP info flow skipped (no ctx)");
		return;
	}

	/* 1) Discover GAP primary service (UUID 0x1800) */
	static const uint8 uuid_gap_1800[ATT_BT_UUID_SIZE] = {
		LO_UINT16(0x1800), HI_UINT16(0x1800)
	};

	bStatus_t s = GATT_DiscPrimaryServiceByUUID(connHandle,
						    (uint8 *)uuid_gap_1800,
						    ATT_BT_UUID_SIZE,
						    selfEntity);
	if (s != SUCCESS) {
		DPRINTF("[INFO] GAP info flow stopped: GAP service discover start failed (0x%02X)",
			s);
		freeNameCtx(connHandle);
		return;
	}
	DPRINTF("[INFO] Discover GAP svc...");
	c->step = DISC_FIND_GAP_SERVICE;
}

static void handleReadByGrpTypeRsp(uint16_t connHandle,
				   const attReadByGrpTypeRsp_t *rsp)
{
	nameDisc_t *c = getNameCtx(connHandle);
	if (!c)
		return;

	if (rsp->len < 4 || !rsp->numGrps || rsp->pDataList == NULL) {
		DPRINTF("[INFO] GAP info flow stopped: GAP service not found");
		c->step = DISC_DONE;
		return;
	}

	const uint8_t *p = rsp->pDataList;
	c->gapStartHdl = BUILD_UINT16(p[0], p[1]);
	c->gapEndHdl = BUILD_UINT16(p[2], p[3]);

	DPRINTF("[INFO] GAP svc [0x%04X..0x%04X]", c->gapStartHdl,
		c->gapEndHdl);

	/* 2) Read Device Name using UUID filter (0x2A00) */
	static const uint8 devNameUUID[ATT_BT_UUID_SIZE] = {
		LO_UINT16(0x2A00), HI_UINT16(0x2A00)
	};

	attReadByTypeReq_t r;
	memset(&r, 0, sizeof(r));
	r.startHandle = c->gapStartHdl;
	r.endHandle = c->gapEndHdl;
	r.type.len = ATT_BT_UUID_SIZE;
	memcpy(r.type.uuid, devNameUUID, ATT_BT_UUID_SIZE);

	bStatus_t s = GATT_ReadUsingCharUUID(connHandle, &r, selfEntity);
	if (s != SUCCESS) {
		DPRINTF("[INFO] GAP info flow stopped: DevName read start failed (0x%02X)",
			s);
		c->step = DISC_DONE;
		return;
	}
	DPRINTF("[INFO] Read Device Name...");
	c->step = DISC_READ_DEVNAME;
}

static void handleReadByTypeRsp_Name(uint16_t connHandle,
				     const attReadByTypeRsp_t *rsp)
{
	nameDisc_t *c = getNameCtx(connHandle);
	if (!c)
		return;

	if (rsp->pDataList == NULL || rsp->len < 3) {
		DPRINTF("[INFO] GAP info flow stopped: DevName char discovery start failed");
		c->step = DISC_DONE;
		return;
	}

	/* Read By Type Rsp: (attrHandle(2) + value(N)), N = rsp->len - 2 */
	const uint8_t *p = rsp->pDataList;
	uint8_t valueLen = rsp->len - 2;
	uint16_t valueHandle = BUILD_UINT16(p[0], p[1]);
	c->devNameValHdl = valueHandle;

	uint8_t copyLen = (valueLen < sizeof(c->prevName) - 1) ?
				  valueLen :
				  (sizeof(c->prevName) - 1);
	memcpy(c->prevName, p + 2, copyLen);
	c->prevName[copyLen] = 0;

	DPRINTF("[INFO] Device Name: \"%s\"",
		c->prevName[0] ? c->prevName : "(empty)");

	/* 3) Try to write a new Device Name */
	c->step = DISC_WRITE_DEVNAME;
	continueNameFlow(connHandle);
}

static void continueNameFlow(uint16_t connHandle)
{
	nameDisc_t *c = getNameCtx(connHandle);
	if (!c)
		return;

	if (c->step == DISC_WRITE_DEVNAME && c->devNameValHdl != 0) {
		static const char kNewName[] = "Secure your device";
		uint8_t len =
			(uint8_t)MIN(sizeof(kNewName) - 1, ATT_MTU_SIZE - 3);

		attWriteReq_t w;
		memset(&w, 0, sizeof(w));
		w.handle = c->devNameValHdl;
		w.sig = FALSE;
		w.cmd = FALSE;
		w.len = len;
		w.pValue = ICall_malloc(len);
		if (!w.pValue) {
			DPRINTF("[INFO] Name write failed to start (no mem)");
			c->step = DISC_DONE;
			return;
		}
		memcpy(w.pValue, kNewName, len);

		bStatus_t s = GATT_WriteCharValue(connHandle, &w, selfEntity);
		if (s != SUCCESS) {
			DPRINTF("[INFO] Name write failed to start (0x%02X)",
				s);
			ICall_free(w.pValue);
			c->step = DISC_DONE;
		} else {
			/* On SUCCESS, the stack owns the buffer and will free it */
			DPRINTF("[INFO] Writing new name...");
		}
	}
}

/* ========================= BondMgr ========================= */

static void JustWorksScanner_passcodeCb(uint8_t *deviceAddr,
					uint16_t connHandle, uint8_t uiInputs,
					uint8_t uiOutputs,
					uint32_t numComparison)
{
	jwPasscodeData_t *pData = ICall_malloc(sizeof(jwPasscodeData_t));
	if (pData) {
		pData->connHandle = connHandle;
		memcpy(pData->deviceAddr, deviceAddr, B_ADDR_LEN);
		pData->uiInputs = uiInputs;
		pData->uiOutputs = uiOutputs;
		pData->numComparison = numComparison;

		if (JustWorksScanner_enqueueMsg(JW_EVT_PASSCODE_NEEDED, 0,
						(uint8_t *)pData) != SUCCESS)
			ICall_free(pData);
	}
}

static void JustWorksScanner_pairStateCb(uint16_t connHandle, uint8_t state,
					 uint8_t status)
{
	jwPairStateData_t *p = ICall_malloc(sizeof(jwPairStateData_t));
	if (p) {
		p->connHandle = connHandle;
		p->status = status;
		if (JustWorksScanner_enqueueMsg(JW_EVT_PAIR_STATE, state,
						(uint8_t *)p) != SUCCESS)
			ICall_free(p);
	}
}

/* ========================= Scan CB / Clocks ================ */

void JustWorksScanner_scanCb(uint32_t evt, void *pMsg, uintptr_t arg)
{
	uint8_t event;
	if (evt & GAP_EVT_ADV_REPORT)
		event = JW_EVT_ADV_REPORT;
	else if (evt & GAP_EVT_SCAN_ENABLED)
		event = JW_EVT_SCAN_ENABLED;
	else if (evt & GAP_EVT_SCAN_DISABLED)
		event = JW_EVT_SCAN_DISABLED;
	else if (evt & GAP_EVT_INSUFFICIENT_MEMORY)
		event = JW_EVT_INSUFFICIENT_MEM;
	else
		return;

	if (JustWorksScanner_enqueueMsg(event, SUCCESS, pMsg) != SUCCESS)
		ICall_free(pMsg);
}

void JustWorksScanner_clockHandler(UArg arg)
{
	uint8_t evtId = (uint8_t)(arg & 0xFF);
	switch (evtId) {
	case JW_EVT_READ_RPA:
		Util_startClock(&clkRpaRead);
		JustWorksScanner_enqueueMsg(JW_EVT_READ_RPA, 0, NULL);
		break;
	case JW_EVT_SCAN_RESTART:
		Util_startClock(&clkScanRestart);
		JustWorksScanner_enqueueMsg(JW_EVT_SCAN_RESTART, 0, NULL);
		break;
	case JW_EVT_SCAN_WATCHDOG:
		Util_startClock(&clkScanWatchdog);
		JustWorksScanner_enqueueMsg(JW_EVT_SCAN_WATCHDOG, 0, NULL);
		break;
	default:
		break;
	}
}

/* ========================= Conn Helpers ==================== */

static uint8_t JustWorksScanner_addConnInfo(uint16_t connHandle, uint8_t *pAddr)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++) {
		if (connList[i].connHandle == LINKDB_CONNHANDLE_INVALID) {
			connList[i].connHandle = connHandle;
			memcpy(connList[i].addr, pAddr, B_ADDR_LEN);
			numConn++;
			return i;
		}
	}
	return MAX_NUM_BLE_CONNS;
}
static uint8_t JustWorksScanner_removeConnInfo(uint16_t connHandle)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++) {
		if (connList[i].connHandle == connHandle) {
			connList[i].connHandle = LINKDB_CONNHANDLE_INVALID;
			numConn--;
			freeNameCtx(connHandle);
			return i;
		}
	}
	return MAX_NUM_BLE_CONNS;
}
static uint8_t JustWorksScanner_getConnIndex(uint16_t connHandle)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
		if (connList[i].connHandle == connHandle)
			return i;
	return MAX_NUM_BLE_CONNS;
}
#ifndef Display_DISABLE_ALL
static char *JustWorksScanner_getConnAddrStr(uint16_t connHandle)
{
	for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
		if (connList[i].connHandle == connHandle)
			return Util_convertBdAddr2Str(connList[i].addr);
	return NULL;
}
#endif

/* ========================= Config / Logs =================== */

static void setBondManagerParameters_ForJustWorks(void)
{
	uint8_t pairMode = GAPBOND_PAIRING_MODE_INITIATE;
	uint8_t ioCap = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
	uint8_t mitm = FALSE;
	uint8_t bonding = TRUE;
	uint8_t sc = TRUE;

	GAPBondMgr_SetParameter(GAPBOND_PAIRING_MODE, sizeof(uint8_t),
				&pairMode);
	GAPBondMgr_SetParameter(GAPBOND_IO_CAPABILITIES, sizeof(uint8_t),
				&ioCap);
	GAPBondMgr_SetParameter(GAPBOND_MITM_PROTECTION, sizeof(uint8_t),
				&mitm);
	GAPBondMgr_SetParameter(GAPBOND_BONDING_ENABLED, sizeof(uint8_t),
				&bonding);
	GAPBondMgr_SetParameter(GAPBOND_SECURE_CONNECTION, sizeof(uint8_t),
				&sc);
}

static void JustWorksScanner_logInsecureJustWorks(uint16_t connHandle,
						  const char *reason)
{
#ifndef Display_DISABLE_ALL
	char *addrStr = JustWorksScanner_getConnAddrStr(connHandle);
	const char *nm = "(no-name)";
	if (addrStr) {
		for (uint8_t i = 0; i < MAX_NUM_BLE_CONNS; i++)
			if (connList[i].connHandle == connHandle) {
				nm = lookupName(connList[i].addr);
				break;
			}
	}
	DPRINTF("[INSECURE] Just Works accepted by peer: addr=%s name=%s %s",
		addrStr ? addrStr : "(unknown)", nm, reason ? reason : "");
#endif
}

static const char *addrTypeStr(uint8_t t)
{
	switch (t) {
	case ADDRTYPE_PUBLIC:
		return "PUBLIC";
	case ADDRTYPE_RANDOM:
		return "RANDOM";
	case ADDRTYPE_PUBLIC_ID:
		return "PUBLIC_ID";
	case ADDRTYPE_RANDOM_ID:
		return "RANDOM_ID";
	default:
		return "UNKNOWN";
	}
}

static const char *phyToStr(uint8_t phy)
{
	switch (phy) {
	case 0x01:
		return "1M";
	case 0x02:
		return "2M";
	case 0x03:
		return "Coded(S2/S8)";
	case 0x00:
		return "(none)";
	default:
		return "?";
	}
}

static const char *evtTypeToStr(uint16_t evtType, char *buf, size_t n)
{
	char tmp[64];
	tmp[0] = 0;
	bool first = true;
#define ADD(bit, txt)                                                       \
	do {                                                                \
		if (evtType & (bit)) {                                      \
			if (!first)                                         \
				strncat(tmp, "|",                           \
					sizeof(tmp) - strlen(tmp) - 1);     \
			strncat(tmp, (txt), sizeof(tmp) - strlen(tmp) - 1); \
			first = false;                                      \
		}                                                           \
	} while (0)
	ADD(ADV_RPT_EVT_TYPE_CONNECTABLE, "CONNECTABLE");
	ADD(ADV_RPT_EVT_TYPE_SCANNABLE, "SCANNABLE");
	ADD(ADV_RPT_EVT_TYPE_DIRECTED, "DIRECTED");
	ADD(ADV_RPT_EVT_TYPE_LEGACY, "LEGACY");
	ADD(ADV_RPT_EVT_TYPE_SCAN_RSP, "SCAN_RSP");
#undef ADD
	if (first)
		strncpy(tmp, "NONE", sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;
	if (n) {
		strncpy(buf, tmp, n - 1);
		buf[n - 1] = 0;
	}
	return buf;
}

/* Robust numeric-only mapping (no SDK macros). Always non-empty. */
static const char *discReasonToStr(uint8_t r)
{
	switch (r) {
	case 0x00:
		return "SUCCESS";
	case 0x01:
		return "UNKNOWN_HCI_CMD";
	case 0x02:
		return "UNKNOWN_CONNECTION";
	case 0x03:
		return "HW_FAILURE";
	case 0x05:
		return "AUTH_FAILURE";
	case 0x06:
		return "PIN_OR_KEY_MISSING";
	case 0x08:
		return "CONN_TIMEOUT";
	case 0x09:
		return "REPEATED_ATTEMPTS";
	case 0x0C:
		return "COMMAND_DISALLOWED";
	case 0x12:
		return "LL_RESPONSE_TIMEOUT";
	case 0x13:
		return "REMOTE_USER_TERM";
	case 0x14:
		return "REMOTE_LOW_RES";
	case 0x15:
		return "REMOTE_POWER_OFF";
	case 0x16:
		return "LOCAL_HOST_TERM";
	case 0x1A:
		return "UNSUPPORTED_FEATURE";
	case 0x28:
		return "PAIRING_NOT_ALLOWED";
	case 0x29:
		return "UNIT_KEY_UNSUPPORTED";
	case 0x2A:
		return "UNACCEPTABLE_CONN_INTERVAL";
	case 0x2F:
		return "MIC_FAILURE";
	case 0x30:
		return "CONN_FAILED_TO_ESTABLISH";
	case 0x3B:
		return "BAD_CONN_PARAMETERS";
	case 0x3D:
		return "MIC_FAILURE";
	case 0x3E:
		return "CONN_FAILED_TO_ESTABLISH";
	default:
		return "UNKNOWN";
	}
}

/* Pairing status helper (best-effort labels) */
static const char *pairStatusToStr(uint8_t s)
{
	switch (s) {
	case 0x00:
		return "SUCCESS";
	case 0x01:
		return "FAIL_PASSKEY_ENTRY";
	case 0x02:
		return "FAIL_OOB_NOT_AVAILABLE";
	case 0x03:
		return "AUTH_REQUIREMENTS";
	case 0x04:
		return "CONFIRM_VALUE_FAILED";
	case 0x05:
		return "AUTH_FAILURE";
	case 0x06:
		return "KEY_MISSING";
	case 0x07:
		return "ENCRYPTION_NOT_ACCEPTED";
	case 0x08:
		return "PAIRING_NOT_SUPPORTED";
	case 0x09:
		return "INSUFFICIENT_AUTHENTICATION";
	case 0x0A:
		return "CONFIRM_FAILED";
	case 0x0B:
		return "NOT_SUPPORTED";
	case 0x0C:
		return "ENCRYPTION_KEY_SIZE";
	case 0x0D:
		return "CMD_NOT_SUPPORTED";
	case 0x0E:
		return "UNSPECIFIED_REASON";
	case 0x0F:
		return "REPEATED_ATTEMPTS";
	case 0x10:
		return "INVALID_PARAMETERS";
	case 0x11:
		return "DHKEY_CHECK_FAILED";
	case 0x12:
		return "NUMERIC_COMPARISON_FAILED";
	case 0x13:
		return "BR_EDR_PAIRING_IN_PROGRESS";
	case 0x14:
		return "CTKD_NOT_ALLOWED";
	case 0x3E:
		return "CONN_FAILED_TO_ESTABLISH";
	default:
		return "UNKNOWN";
	}
}

/* ========================= EOF ============================= */
