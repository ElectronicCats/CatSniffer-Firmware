#include "smartrf_settings.h"
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/rf_prop_cmd.h)
#include DeviceFamily_constructPath(rf_patches/rf_patch_cpe_prop.h)

// -----------------------------------------------------------------------------
// OOK 433.92 MHz
// -----------------------------------------------------------------------------
rfc_CMD_PROP_RADIO_DIV_SETUP_t RF_cmdPropRadioDivSetup_OOK =
{
    .commandNo = 0x3807,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .condition.nSkip = 0x00,
    .modulation.modType = 0x2, // OOK
    .modulation.deviation = 0x0,
    .modulation.deviationStepSz = 0x0,
    .symbolRate.preScale = 0xF,
    .symbolRate.rateWord = 0x8000, // 4.8 kbps
    .symbolRate.decimMode = 0x0,
    .rxBw = 0x24, // 195 kHz
    .preamConf.nPreamBytes = 0x4,
    .preamConf.preamMode = 0x0,
    .formatConf.nSwBits = 0x18, // 24 bit sync
    .formatConf.bBitReversal = 0x0,
    .formatConf.bMsbFirst = 0x1,
    .formatConf.fecMode = 0x0,
    .formatConf.whitenMode = 0x0,
    .config.frontEndMode = 0x0,
    .config.biasMode = 0x1,
    .config.analogCfgMode = 0x0,
    .config.bNoFsPowerUp = 0x0,
    .txPower = 0x013F, // 0dBm default
    .pRegOverride = 0, // Should add overrides for CC1352P7
    .centerFreq = 0x01B1, // 433
    .intFreq = 0x0800,
    .loDivider = 0x00
};

rfc_CMD_FS_t RF_cmdFs_OOK =
{
    .commandNo = 0x0803,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .frequency = 0x01B1, // 433
    .fractFreq = 0xEB85, // .92
    .synthConf.bTxMode = 0x0,
    .synthConf.refFreq = 0x0,
    .__dummy0 = 0x00,
    .__dummy1 = 0x00,
    .__dummy2 = 0x00,
    .__dummy3 = 0x0000
};

rfc_CMD_PROP_TX_t RF_cmdPropTx_OOK =
{
    .commandNo = 0x3801,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .pktConf.bFsOff = 0x0,
    .pktConf.bUseCrc = 0x0,
    .pktConf.bVarLen = 0x0,
    .pktLen = 0, // Up to max
    .syncWord = 0x930B51DE,
    .pPkt = 0
};

rfc_CMD_PROP_RX_t RF_cmdPropRx_OOK =
{
    .commandNo = 0x3802,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .pktConf.bFsOff = 0x0,
    .pktConf.bRepeatOk = 0x0,
    .pktConf.bRepeatNok = 0x0,
    .pktConf.bUseCrc = 0x0,
    .pktConf.bVarLen = 0x0,
    .pktConf.bChkAddress = 0x0,
    .pktConf.endType = 0x0,
    .pktConf.filterOp = 0x0,
    .rxConf.bAutoFlushIgnored = 0x0,
    .rxConf.bAutoFlushCrcErr = 0x0,
    .rxConf.bIncludeHdr = 0x1,
    .rxConf.bIncludeCrc = 0x0,
    .rxConf.bAppendRssi = 0x1,
    .rxConf.bAppendTimestamp = 0x1,
    .rxConf.bAppendStatus = 0x1,
    .syncWord = 0x00000000, // No sync word for raw? Actually for rx raw we might use different command
    .maxPktLen = 255,
    .address0 = 0xAA,
    .address1 = 0xBB,
    .pQueue = 0,
    .pOutput = 0
};

// -----------------------------------------------------------------------------
// FSK 50kbps, 25kHz dev (Generic)
// -----------------------------------------------------------------------------
rfc_CMD_PROP_RADIO_DIV_SETUP_t RF_cmdPropRadioDivSetup_FSK =
{
    .commandNo = 0x3807,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .condition.nSkip = 0x00,
    .modulation.modType = 0x1, // 2-GFSK
    .modulation.deviation = 0x64, // 25 kHz
    .modulation.deviationStepSz = 0x0,
    .symbolRate.preScale = 0xF,
    .symbolRate.rateWord = 0x8000, // 50 kbps
    .symbolRate.decimMode = 0x0,
    .rxBw = 0x24, // 100 kHz (approx)
    .preamConf.nPreamBytes = 0x4,
    .preamConf.preamMode = 0x0,
    .formatConf.nSwBits = 0x20, // 32 bit sync
    .formatConf.bBitReversal = 0x0,
    .formatConf.bMsbFirst = 0x1,
    .formatConf.fecMode = 0x0,
    .formatConf.whitenMode = 0x0,
    .config.frontEndMode = 0x0, 
    .config.biasMode = 0x1,
    .config.analogCfgMode = 0x0,
    .config.bNoFsPowerUp = 0x0,
    .txPower = 0x013F, // 0dBm
    .pRegOverride = 0,
    .centerFreq = 0x0393, // 915 MHz
    .intFreq = 0x0800,
    .loDivider = 0x00
};

rfc_CMD_FS_t RF_cmdFs_FSK =
{
    .commandNo = 0x0803,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .frequency = 0x0393, // 915
    .fractFreq = 0x0000,
    .synthConf.bTxMode = 0x0,
    .synthConf.refFreq = 0x0,
    .__dummy0 = 0x00,
    .__dummy1 = 0x00,
    .__dummy2 = 0x00,
    .__dummy3 = 0x0000
};

rfc_CMD_PROP_TX_t RF_cmdPropTx_FSK =
{
    .commandNo = 0x3801,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .pktConf.bFsOff = 0x0,
    .pktConf.bUseCrc = 0x1,
    .pktConf.bVarLen = 0x1,
    .pktLen = 0,
    .syncWord = 0x930B51DE,
    .pPkt = 0
};

rfc_CMD_PROP_RX_t RF_cmdPropRx_FSK =
{
    .commandNo = 0x3802,
    .status = 0x0000,
    .pNextOp = 0,
    .startTime = 0x00000000,
    .startTrigger.triggerType = 0x0,
    .condition.rule = 0x1,
    .pktConf.bFsOff = 0x0,
    .pktConf.bRepeatOk = 0x0,
    .pktConf.bRepeatNok = 0x0,
    .pktConf.bUseCrc = 0x1,
    .pktConf.bVarLen = 0x1,
    .pktConf.bChkAddress = 0x0,
    .pktConf.endType = 0x0,
    .pktConf.filterOp = 0x0,
    .rxConf.bAutoFlushIgnored = 0x0,
    .rxConf.bAutoFlushCrcErr = 0x0,
    .rxConf.bIncludeHdr = 0x1,
    .rxConf.bIncludeCrc = 0x1,
    .rxConf.bAppendRssi = 0x1,
    .rxConf.bAppendTimestamp = 0x1,
    .rxConf.bAppendStatus = 0x1,
    .syncWord = 0x930B51DE,
    .maxPktLen = 255,
    .address0 = 0xAA,
    .address1 = 0xBB,
    .pQueue = 0,
    .pOutput = 0
};

// Overrides for CC1352P7 High PA (20dBm) supported if needed, using default 0dBm path for now.
uint32_t pOverrides[] =
{
    // overrides_prop_common.xml
    0x00F388D3,
    0x002B0030, // HACK: Might need specific overrides for CC1352P7
    0xFFFFFFFF
};

void SmartRF_init() {
    RF_cmdPropRadioDivSetup_OOK.pRegOverride = pOverrides;
    RF_cmdPropRadioDivSetup_FSK.pRegOverride = pOverrides;
}

// RF_prop definition
RF_Mode RF_prop =
{
    .rfMode = RF_MODE_AUTO,
    .cpePatchFxn = &rf_patch_cpe_prop,
    .mcePatchFxn = 0,
    .rfePatchFxn = 0
};
