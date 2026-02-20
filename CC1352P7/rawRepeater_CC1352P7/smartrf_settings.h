#ifndef SMARTRF_SETTINGS_H
#define SMARTRF_SETTINGS_H

#include <ti/drivers/rf/RF.h>
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/rf_prop_cmd.h)

// OOK Settings
extern rfc_CMD_PROP_RADIO_DIV_SETUP_t RF_cmdPropRadioDivSetup_OOK;
extern rfc_CMD_FS_t RF_cmdFs_OOK;
extern rfc_CMD_PROP_TX_t RF_cmdPropTx_OOK;
extern rfc_CMD_PROP_RX_t RF_cmdPropRx_OOK;

// FSK Settings
extern rfc_CMD_PROP_RADIO_DIV_SETUP_t RF_cmdPropRadioDivSetup_FSK;
extern rfc_CMD_FS_t RF_cmdFs_FSK;
extern rfc_CMD_PROP_TX_t RF_cmdPropTx_FSK;
extern rfc_CMD_PROP_RX_t RF_cmdPropRx_FSK;
extern RF_Mode RF_prop; // Standard packet RX

#endif // SMARTRF_SETTINGS_H
