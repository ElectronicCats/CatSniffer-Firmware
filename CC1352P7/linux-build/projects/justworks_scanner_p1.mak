# Just Works scanner (rol central) para el CC1352P1 de la CatSniffer v2.
#
# Este proyecto no existe en la rama v2: se compila desde las MISMAS fuentes C
# que el P7, con un .syscfg propio que declara el board CC1352P_2_LAUNCHXL y su
# diseño de RF. Solo cambian SYSCFG_FILE, SDK_BOARD y DEVICE_FAMILY.
PROJECT_DIR   := justworks_scanner_CC1352P7_1
TARGET_NAME   := justworks_scanner_CC1352P1
APP_SOURCES   := Application/justworks_scanner.c
SYSCFG_FILE   := justworks_scanner_p1.syscfg
SDK_REF       := simple_central
SDK_BOARD     := CC1352P_2_LAUNCHXL
DEVICE_FAMILY := cc13x2_cc26x2
