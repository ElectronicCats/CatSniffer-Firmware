# Apple AirTag scanner (rol central) para el CC1352P1 de la CatSniffer v2.
#
# Las fuentes no están en esta rama: viven en origin/v1.x/v2.x, bajo
# CC1352P2/. V2_SRC_ROOT (paths.mk) dice dónde está ese árbol.
PROJECT_DIR   := airtag_scanner_CC1352P_2
SRC_ROOT      := $(V2_SRC_ROOT)
# Debe contener "cc1352p1": catnip clasifica la imagen por el nombre del
# archivo (modules/firmware/board.py), y "CC1352P_2" no matchea nada.
TARGET_NAME   := airtag_scanner_CC1352P1
APP_SOURCES   := Application/airtag_scanner.c
SYSCFG_FILE   := airtag_scanner.syscfg
SDK_REF       := simple_central
SDK_BOARD     := CC1352P_2_LAUNCHXL
DEVICE_FAMILY := cc13x2_cc26x2
