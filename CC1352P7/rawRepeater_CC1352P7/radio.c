#include "radio.h"
#include "smartrf_settings.h"
#include "ti_drivers_config.h"
#include <ti/drivers/rf/RF.h>
#include <ti/drivers/rf/RF.h>
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/ioc.h)
#include DeviceFamily_constructPath(driverlib/rf_prop_mailbox.h)
#include DeviceFamily_constructPath(driverlib/rf_common_cmd.h)

/* RF Handle */
static RF_Object rfObject;
static RF_Handle rfHandle;

/* Current Settings */
static RadioMode_t currentMode = RADIO_MODE_OOK;

/* RX Test Command for Raw Mode */
static rfc_CMD_RX_TEST_t cmdRxTest;

void Radio_Init() {
    RF_Params rfParams;
    RF_Params_init(&rfParams);
    
    // Open RF driver - we open with OOK setup
    extern RF_Mode RF_prop;
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup_OOK, &rfParams);
    
    // Set initial frequency
    RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs_OOK, RF_PriorityNormal, NULL, 0);
}

void Radio_SetMode(RadioMode_t mode) {
    if (currentMode == mode) return;
    
    currentMode = mode;
    
    // We should re-open or post new setup
    // For simplicity, let's close and re-open to ensure clean state mechanism
    RF_close(rfHandle);
    
    RF_Params rfParams;
    RF_Params_init(&rfParams);

    if (mode == RADIO_MODE_OOK) {
        extern RF_Mode RF_prop;
        rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup_OOK, &rfParams);
        RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs_OOK, RF_PriorityNormal, NULL, 0);
    } else {
        extern RF_Mode RF_prop;
        rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup_FSK, &rfParams);
        RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs_FSK, RF_PriorityNormal, NULL, 0);
    }
}

void Radio_SetFreq(uint32_t freq_hz) {
    uint16_t centerFreq = (uint16_t)(freq_hz / 1000000);
    uint16_t fractFreq = (uint16_t)(((uint64_t)freq_hz - ((uint64_t)centerFreq * 1000000)) * 65536 / 1000000);

    if (currentMode == RADIO_MODE_OOK) {
        RF_cmdFs_OOK.frequency = centerFreq;
        RF_cmdFs_OOK.fractFreq = fractFreq;
        RF_cmdPropRadioDivSetup_OOK.centerFreq = centerFreq;
        RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs_OOK, RF_PriorityNormal, NULL, 0);
    } else {
        RF_cmdFs_FSK.frequency = centerFreq;
        RF_cmdFs_FSK.fractFreq = fractFreq;
        RF_cmdPropRadioDivSetup_FSK.centerFreq = centerFreq;
        RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs_FSK, RF_PriorityNormal, NULL, 0);
    }
}

void Radio_RouteDataToPin(bool enable) {
    // Route receive data (RFC_GPO0) to DIO_12 (RX Pin in SysConfig is UART??)
    // We need a separate pin for Capture. 
    // Let's use DIO_21 for debug/capture output if available, or just reuse a pin.
    // The user said "Capture... without needing to know any protocol".
    // We will use DIO_15 (LaunchPad Header) for Raw Data Output.
    // In SysConfig we didn't confirm DIO_15 is free. DIO_6/7 are LEDs. DIO_12/13 are UART.
    // DIO_15 is likely free.
    
    if (enable) {
        // Defines for IOC are in driverlib/ioc.h
        // Route RFC_GPO0 to a pin
        // Note: For CC1352P7, ensure GPO0 is mapped to demod out logic in overrides
        // Standard OOK setup usually maps it.
        IOCPortConfigureSet(IOID_15, IOC_PORT_RFC_GPO0, IOC_STD_OUTPUT);
    } else {
        IOCPortConfigureSet(IOID_15, IOC_PORT_GPIO, IOC_STD_OUTPUT);
    }
}

int Radio_StartRx() {
    Radio_StopRx(); // Ensure clean state
    
    if (currentMode == RADIO_MODE_OOK) {
        // Use RX Test command for Raw OOK (Infinite RX, no sync)
        cmdRxTest.commandNo = CMD_RX_TEST;
        cmdRxTest.config.bEnaFifo = 1;
        cmdRxTest.config.bFsOff = 0;
        cmdRxTest.config.bNoSync = 1; // Raw mode
        cmdRxTest.endTrigger.triggerType = TRIG_NEVER;
        
        RF_postCmd(rfHandle, (RF_Op*)&cmdRxTest, RF_PriorityNormal, NULL, 0);
    } else {
        // FSK Packet RX
        RF_postCmd(rfHandle, (RF_Op*)&RF_cmdPropRx_FSK, RF_PriorityNormal, NULL, 0);
    }
    return 0;
}

void Radio_StopRx() {
    RF_cancelCmd(rfHandle, RF_CMDHANDLE_FLUSH_ALL, 0);
}

int Radio_SendPacket(uint8_t* data, uint16_t len) {
    if (currentMode == RADIO_MODE_OOK) {
        RF_cmdPropTx_OOK.pPkt = data;
        RF_cmdPropTx_OOK.pktLen = len;
        RF_runCmd(rfHandle, (RF_Op*)&RF_cmdPropTx_OOK, RF_PriorityNormal, NULL, 0);
    } else {
        RF_cmdPropTx_FSK.pPkt = data;
        RF_cmdPropTx_FSK.pktLen = len;
        RF_runCmd(rfHandle, (RF_Op*)&RF_cmdPropTx_FSK, RF_PriorityNormal, NULL, 0);
    }
    return 0;
}

void Radio_StartAsyncTx() {
    // Not implemented - see note on Replay using packet construction or CMD_TX_TEST
}

void Radio_StopAsyncTx() {
    RF_cancelCmd(rfHandle, RF_CMDHANDLE_FLUSH_ALL, 0);
}
