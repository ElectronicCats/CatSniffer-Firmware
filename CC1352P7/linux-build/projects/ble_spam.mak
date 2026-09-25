# BLE spam (rol periférico/broadcaster) — payload cycling Apple/Android/Windows/Samsung.
#
# Los módulos extra replican los de airtag_spoofer.mak (ORDERED_OBJS del
# proyecto CCS de referencia): rcosc_calibration, NPI + icall_hci_tl, y los
# perfiles devinfoservice y simple_gatt_profile.
#
# USE_MENU compila board_key.c (BTN-1/BTN-2) y two_btn_menu.c; el firmware solo
# usa Board_initKeys() + su propio handler (BTN-1 = start/stop, BTN-2 = modo),
# el framework two_btn_menu no se referencia y el linker descarta sus secciones.
PROJECT_DIR  := ble_spam_CC1352P_7
TARGET_NAME  := ble_spam_CC1352P_7
APP_SOURCES  := Application/ble_spam.c Application/ble_spam_models.c
SYSCFG_FILE  := ble_spam.syscfg
SDK_REF      := simple_peripheral
USE_RCOSC    := 1
USE_NPI      := 1
USE_PROFILES := 1
USE_MENU     := 1

# Fase 5 (stretch): coexistencia spam + sniffing. Con SPAM_WITH_SCAN := 1 el
# firmware inicializa el rol combinado Peripheral+Observer y expone el comando
# UART "scan on|off"; common.mak redefine HOST_CONFIG a (PERIPHERAL_CFG|
# OBSERVER_CFG) para habilitar el scanner en la jump-table icall-lite. Por
# defecto 0: el binario es idéntico al build advertise-only (sin regresión).
SPAM_WITH_SCAN := 0
