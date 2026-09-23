# BLE spam (rol periférico/broadcaster) — payload cycling Apple/Android/Windows/Samsung.
#
# Los módulos extra replican los de airtag_spoofer.mak (ORDERED_OBJS del
# proyecto CCS de referencia): rcosc_calibration, NPI + icall_hci_tl, y los
# perfiles devinfoservice y simple_gatt_profile.
PROJECT_DIR  := ble_spam_CC1352P_7
TARGET_NAME  := ble_spam_CC1352P_7
APP_SOURCES  := Application/ble_spam.c Application/ble_spam_models.c
SYSCFG_FILE  := ble_spam.syscfg
SDK_REF      := simple_peripheral
USE_RCOSC    := 1
USE_NPI      := 1
USE_PROFILES := 1
