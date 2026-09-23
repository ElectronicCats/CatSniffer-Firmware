# Ubicación de las herramientas de TI.
#
# Cada variable usa ?= : cualquiera puede sobreescribirse desde el entorno
#   TICLANG_ARMCOMPILER=/otra/ruta make airtag_scanner
# o de forma permanente creando paths.local.mk (ignorado por git).

TI_ROOT ?= $(HOME)/ti

# El SDK se busca bajo $(TI_ROOT) en vez de fijar una versión: sirve cualquier
# simplelink_cc13xx_cc26xx_sdk_* que traiga ble5stack. Según cómo se haya
# pasado --prefix al instalador .run, el SDK queda directamente en el prefijo o
# en un directorio anidado con el mismo nombre; se aceptan ambas formas y, si
# hay varias, gana la última por orden alfabético (la versión más reciente).
SDK_CANDIDATES := \
	$(wildcard $(TI_ROOT)/simplelink_cc13xx_cc26xx_sdk_*/source/ti/ble5stack) \
	$(wildcard $(TI_ROOT)/simplelink_cc13xx_cc26xx_sdk_*/simplelink_cc13xx_cc26xx_sdk_*/source/ti/ble5stack)

SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR ?= \
	$(patsubst %/source/ti/ble5stack,%,$(lastword $(sort $(SDK_CANDIDATES))))

# SysConfig y XDCtools vienen con el SDK: quedan dentro del prefijo de
# instalación (junto al SDK) o, si el prefijo fue el propio $(TI_ROOT), ahí.
SDK_PREFIX := $(patsubst %/,%,$(dir $(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)))

SYSCONFIG_TOOL ?= $(firstword \
	$(wildcard $(SDK_PREFIX)/sysconfig_*/sysconfig_cli.sh) \
	$(wildcard $(TI_ROOT)/sysconfig_*/sysconfig_cli.sh))

XDC_INSTALL_DIR ?= $(firstword \
	$(wildcard $(SDK_PREFIX)/xdctools_*_core) \
	$(wildcard $(TI_ROOT)/xdctools_*_core))

# El instalador de tiarmclang también anida el directorio.
TICLANG_ARMCOMPILER ?= $(TI_ROOT)/ti-cgt-armllvm_3.2.2.LTS/ti-cgt-armllvm_3.2.2.LTS

PYTHON ?= python3

# Raíz de las fuentes de la CatSniffer v2 (CC1352P1). Viven en la rama
# origin/v1.x/v2.x, bajo CC1352P2/. Para compilarlas desde esta rama:
#   git worktree add --detach ../../../CatSniffer-Firmware-v2 origin/v1.x/v2.x
V2_SRC_ROOT ?= $(abspath $(REPO_DIR)/../../CatSniffer-Firmware-v2/CC1352P2)
