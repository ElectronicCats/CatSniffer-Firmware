# Apple AirTag spoofer (rol periférico) para el CC1352P1 de la CatSniffer v2.
#
# Mismos módulos opcionales que el proyecto P7: rcosc_calibration, la pila NPI
# + icall_hci_tl y los perfiles devinfoservice / simple_gatt_profile.
PROJECT_DIR   := airtag_spoofer_CC1352P_2_LAUNCHXL_tirtos7_ticlang
SRC_ROOT      := $(V2_SRC_ROOT)
TARGET_NAME   := airtag_spoofer_CC1352P1
APP_SOURCES   := Application/airtag_spoofer.c
SYSCFG_FILE   := airtag_spoofer.syscfg
SDK_REF       := simple_peripheral
SDK_BOARD     := CC1352P_2_LAUNCHXL
DEVICE_FAMILY := cc13x2_cc26x2
USE_RCOSC     := 1
USE_NPI       := 1
USE_PROFILES  := 1
