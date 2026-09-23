# Apple AirTag spoofer (rol periférico).
#
# Los módulos extra salen de ORDERED_OBJS del proyecto CCS original, que
# enlazaba rcosc_calibration, la pila NPI + icall_hci_tl, y los perfiles
# devinfoservice y simple_gatt_profile.
PROJECT_DIR  := airtag_spoofer_CC1352P_7
TARGET_NAME  := airtag_spoofer_CC1352P_7
APP_SOURCES  := Application/airtag_spoofer.c
SYSCFG_FILE  := simple_peripheral.syscfg
SDK_REF      := simple_peripheral
USE_RCOSC    := 1
USE_NPI      := 1
USE_PROFILES := 1
