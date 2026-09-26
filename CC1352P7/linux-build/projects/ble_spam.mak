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

# Coexistencia spam + sniffing: el escaneo BLE pasivo forma parte del build por
# defecto. Toda la configuración —el default SPAM_WITH_SCAN=1 y el acoplamiento
# con el rol Observer del stack (HOST_CONFIG)— vive ahora en el árbol de fuentes
# del proyecto, en Application/ble_spam_config.h, que se fuerza al inicio de cada
# unidad de traducción vía este hook portátil (common.mak lo traduce a -include).
# linux-build no contiene ninguna lógica específica de ble_spam: el firmware es
# autocontenido y su config viaja con el código (portable a CCS u otro build).
PROJECT_PREINCLUDE := Application/ble_spam_config.h

# Opt-out sin editar código, para builds sin scanner (binario advertise-only
# histórico, sin breaking changes): `make ble_spam SPAM_WITH_SCAN=0`. Solo se
# propaga si el usuario lo pide por la línea de make; el default (1) lo pone el
# header. El #ifndef de ble_spam_config.h respeta este -D, y con valor 0 ni se
# redefine HOST_CONFIG ni se dispara el #error del invariante.
ifdef SPAM_WITH_SCAN
PROJECT_CFLAGS += -DSPAM_WITH_SCAN=$(SPAM_WITH_SCAN)
endif
