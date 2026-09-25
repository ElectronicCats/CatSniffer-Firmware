# Reglas de build compartidas por los firmwares BLE del CC1352P7.
#
# No se invoca directamente: usa el Makefile de este directorio
#   make airtag_scanner
#
# Cada firmware aporta sus variables en projects/<nombre>.mak.

ifeq ($(PROJECT),)
$(error Define PROJECT, o mejor usa el Makefile de este directorio: make <firmware>)
endif

BUILD_ROOT := $(patsubst %/,%,$(dir $(abspath $(firstword $(MAKEFILE_LIST)))))
REPO_DIR   := $(abspath $(BUILD_ROOT)/..)

-include $(BUILD_ROOT)/paths.local.mk
include $(BUILD_ROOT)/paths.mk
include $(BUILD_ROOT)/projects/$(PROJECT).mak

SDK     := $(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)

# Board del SDK y familia de dispositivo: P7 por defecto, los proyectos P1 las
# sobreescriben con := (projects/<nombre>.mak se incluye antes que esto, así
# que una asignación con := gana sobre estos ?=).
SDK_BOARD     ?= LP_CC1352P7_1
DEVICE_FAMILY ?= cc13x2x7_cc26x2x7

# Raíz de la que cuelga PROJECT_DIR. Por defecto CC1352P7/..; los proyectos P1
# apuntan al árbol v2 (ver V2_SRC_ROOT en paths.mk).
SRC_ROOT ?= $(REPO_DIR)

SRC_DIR := $(SRC_ROOT)/$(PROJECT_DIR)
OUT     := $(BUILD_ROOT)/out/$(PROJECT)
SYSCFG_OUT := $(OUT)/syscfg

# Ejemplo del SDK del que se toma el boilerplate que el proyecto CCS no
# versiona con cambios propios (osal_icall_ble.c y el linker command file).
SDK_EX  := $(SDK)/examples/rtos/$(SDK_BOARD)/ble5stack/$(SDK_REF)

CC  := "$(TICLANG_ARMCOMPILER)/bin/tiarmclang"
LNK := "$(TICLANG_ARMCOMPILER)/bin/tiarmclang"
HEX := "$(TICLANG_ARMCOMPILER)/bin/tiarmhex"

# ---------------------------------------------------------------- SysConfig

SYSCFG_CMD := $(SYSCONFIG_TOOL) --compiler ticlang --product $(SDK)/.metadata/product.json
SYSCFG_IN  := $(SRC_DIR)/$(SYSCFG_FILE)

SYSCFG_FILES := $(shell $(SYSCFG_CMD) --listGeneratedFiles --listReferencedFiles \
                        --output $(SYSCFG_OUT) $(SYSCFG_IN))
SYSCFG_C_FILES   := $(filter %.c,$(SYSCFG_FILES))
SYSCFG_H_FILES   := $(filter %.h,$(SYSCFG_FILES))
SYSCFG_OPT_FILES := $(filter %.opt,$(SYSCFG_FILES))

# ------------------------------------------------------------------ Fuentes

# Del repositorio: la aplicación y su main.c.
PROJ_SRCS := $(addprefix $(SRC_DIR)/,$(APP_SOURCES)) $(SRC_DIR)/Startup/main.c

# Del SDK: osal_icall_ble.c se toma del ejemplo de referencia porque el del
# repositorio está escrito contra el SDK 7.10, donde GATTServApp_Init() tenía
# un parámetro menos.
SDK_SRCS := \
	$(SDK_EX)/Startup/osal_icall_ble.c \
	$(SDK)/source/ti/ble5stack/common/cc26xx/util.c \
	$(SDK)/source/ti/common/nv/crc.c \
	$(SDK)/source/ti/common/nv/nvocmp.c \
	$(SDK)/source/ti/ble5stack/icall/src/icall.c \
	$(SDK)/source/ti/ble5stack/icall/src/icall_cc2650.c \
	$(SDK)/source/ti/ble5stack/icall/src/icall_user_config.c \
	$(SDK)/source/ti/ble5stack/icall/app/ble_user_config.c \
	$(SDK)/source/ti/ble5stack/icall/stack/ble_user_config_stack.c \
	$(SDK)/source/ti/ble5stack/icall/app/icall_api_lite.c \
	$(SDK)/source/ti/ble5stack/rom/agama_r1/rom_init.c

# Módulos opcionales: los activa cada projects/<nombre>.mak según lo que el
# proyecto CCS original enlazaba.
ifeq ($(USE_RCOSC),1)
SDK_SRCS += $(SDK)/source/ti/ble5stack/common/cc26xx/rcosc/rcosc_calibration.c
endif

ifeq ($(USE_NPI),1)
SDK_SRCS += \
	$(SDK)/source/ti/ble5stack/npi/src/npi_tl.c \
	$(SDK)/source/ti/ble5stack/npi/src/npi_rxbuf.c \
	$(SDK)/source/ti/ble5stack/npi/src/npi_task.c \
	$(SDK)/source/ti/ble5stack/npi/src/npi_frame_hci.c \
	$(SDK)/source/ti/ble5stack/npi/src/npi_tl_uart.c \
	$(SDK)/source/ti/ble5stack/icall/app/icall_hci_tl.c
endif

ifeq ($(USE_PROFILES),1)
SDK_SRCS += \
	$(SDK)/source/ti/ble5stack/profiles/dev_info/cc26xx/devinfoservice.c \
	$(SDK)/source/ti/ble5stack/profiles/simple_profile/cc26xx/simple_gatt_profile.c
endif

ifeq ($(USE_MENU),1)
SDK_SRCS += \
	$(SDK)/source/ti/ble5stack/common/cc26xx/board_key.c \
	$(SDK)/source/ti/ble5stack/common/cc26xx/menu/two_btn_menu.c
endif

ALL_SRCS := $(PROJ_SRCS) $(SDK_SRCS) $(SYSCFG_C_FILES)
OBJECTS  := $(addprefix $(OUT)/,$(patsubst %.c,%.obj,$(notdir $(ALL_SRCS))))

# -------------------------------------------------------------------- Flags

# Tomadas del makefile gmake del ejemplo del SDK, más:
#   -include compat_sdk8.h  para los renombres de API 7.10 -> 8.30
#   los include del propio proyecto
CFLAGS := \
	-I$(SRC_DIR) \
	-I$(SRC_DIR)/Application \
	-I$(SYSCFG_OUT) \
	-include $(BUILD_ROOT)/compat_sdk8.h \
	$(addprefix @,$(SYSCFG_OPT_FILES)) \
	-mcpu=cortex-m4 \
	-mthumb \
	-mlittle-endian \
	-mfloat-abi=hard \
	-mfpu=fpv4-sp-d16 \
	-march=armv7e-m \
	-std=c99 \
	-ffunction-sections \
	-g \
	-gdwarf-3 \
	-Oz \
	-DFLASH_ROM_BUILD \
	-DNVOCMP_NWSAMEITEM=1 \
	-DHEAPMGR_CONFIG=0x80 \
	-DHEAPMGR_SIZE=0x0 \
	"-I$(SDK)/source" \
	"-I$(SDK)/source/ti" \
	"-I$(SDK)/source/ti/ble5stack" \
	"-I$(SDK)/source/ti/ble5stack/controller/cc26xx/inc" \
	"-I$(SDK)/source/ti/ble5stack/inc" \
	"-I$(SDK)/source/ti/ble5stack/rom" \
	"-I$(SDK)/source/ti/ble5stack/common/cc26xx" \
	"-I$(SDK)/source/ti/ble5stack/common/cc26xx/rcosc" \
	"-I$(SDK)/source/ti/ble5stack/icall/inc" \
	"-I$(SDK)/source/ti/ble5stack/hal/src/target/_common" \
	"-I$(SDK)/source/ti/ble5stack/common/cc26xx/npi/stack" \
	"-I$(SDK)/source/ti/ble5stack/hal/src/inc" \
	"-I$(SDK)/source/ti/ble5stack/heapmgr" \
	"-I$(SDK)/source/ti/ble5stack/profiles/dev_info" \
	"-I$(SDK)/source/ti/ble5stack/profiles/simple_profile" \
	"-I$(SDK)/source/ti/ble5stack/icall/src/inc" \
	"-I$(SDK)/source/ti/ble5stack/npi/src" \
	"-I$(SDK)/source/ti/ble5stack/npi/src/inc" \
	"-I$(SDK)/source/ti/ble5stack/osal/src/inc" \
	"-I$(SDK)/source/ti/ble5stack/services/src/saddr" \
	"-I$(SDK)/source/ti/ble5stack/services/src/sdata" \
	"-I$(SDK)/source/ti/common/nv" \
	"-I$(SDK)/source/ti/common/cc26xx" \
	"-I$(SDK)/source/ti/ble5stack/icall/src" \
	"-I$(SDK)/source/ti/bleapp/profiles/health_thermometer" \
	"-I$(SDK)/source/ti/bleapp/services/health_thermometer" \
	"-I$(SDK)/source/ti/devices/$(DEVICE_FAMILY)" \
	"-I$(SDK)/kernel/tirtos7/packages" \
	"-I$(SDK)/source/ti/posix/ticlang" \
	"@$(SDK)/source/ti/ble5stack/config/build_components.opt" \
	"@$(SDK)/source/ti/ble5stack/config/factory_config.opt"

# Fase 5 (stretch, solo ble_spam): coexistencia spam + sniffing. SysConfig emite
# -DHOST_CONFIG=PERIPHERAL_CFG en ti_utils_build_compiler.opt (incluido arriba con
# @); cuando el proyecto pide SPAM_WITH_SCAN=1 lo redefinimos al rol combinado
# Peripheral+Observer. bcomdef.h reconoce (PERIPHERAL_CFG|OBSERVER_CFG) como
# configuración de primera clase y deriva CTRL_CONFIG con SCAN_CFG; la -U previa
# evita el warning de redefinición. Estas flags van tras el @opt, así que ganan
# en la línea de compilación. Con SPAM_WITH_SCAN distinto de 1 no se añade nada
# (binario idéntico al build advertise-only, y el resto de firmwares intactos).
ifeq ($(SPAM_WITH_SCAN),1)
CFLAGS += -DSPAM_WITH_SCAN=1 -UHOST_CONFIG "-DHOST_CONFIG=(PERIPHERAL_CFG|OBSERVER_CFG)"
endif

LFLAGS := \
	-Wl,--diag_wrap=off \
	-Wl,--display_error_number \
	-Wl,-x \
	-Wl,--diag_suppress=16002-D \
	-Wl,--diag_suppress=10247-D \
	-Wl,--diag_suppress=10325-D \
	-Wl,--diag_suppress=10229-D \
	-Wl,--diag_suppress=16032-D \
	-Wl,-c \
	"-L$(SDK)/source" \
	"-L$(SDK)/kernel/tirtos7/packages" \
	"-L$(SYSCFG_OUT)" \
	-Wl,--define=FLASH_ROM_BUILD=2 \
	$(SYSCFG_OUT)/ti_utils_build_linker.cmd.genlibs \
	$(SDK_EX)/tirtos7/ticlang/$(DEVICE_FAMILY)_app_tirtos7.cmd \
	"-Wl,-m,$(OUT)/$(TARGET_NAME).map" \
	-Wl,--rom_model \
	-Wl,--warn_sections \
	"-L$(TICLANG_ARMCOMPILER)/lib" \
	-llibc.a

# ------------------------------------------------------------------- Reglas

.PHONY: all clean
all: $(OUT)/$(TARGET_NAME).hex

$(OUT) $(SYSCFG_OUT):
	@mkdir -p $@

.INTERMEDIATE: syscfg
$(SYSCFG_FILES): syscfg
	@:

syscfg: $(SYSCFG_IN) | $(SYSCFG_OUT)
	@echo "[$(PROJECT)] SysConfig: $(notdir $(SYSCFG_IN))"
	@$(SYSCFG_CMD) --output $(SYSCFG_OUT) $(SYSCFG_IN) > /dev/null

# Una regla por fuente: los objetos se aplanan a $(OUT)/<basename>.obj, así que
# dos fuentes con el mismo nombre de archivo colisionarían (hoy no ocurre).
define C_RULE
$(OUT)/$(basename $(notdir $(1))).obj: $(1) $(SYSCFG_H_FILES) | $(OUT)
	@echo "[$(PROJECT)] CC $$(notdir $$<)"
	@$(CC) $(CFLAGS) -c $$< -o $$@
endef
$(foreach src,$(ALL_SRCS),$(eval $(call C_RULE,$(src))))

$(OUT)/$(TARGET_NAME).out: $(OBJECTS)
	@echo "[$(PROJECT)] LD $(notdir $@)"
	@$(LNK) -Wl,-u,_c_int00 $(OBJECTS) $(LFLAGS) -o $@

# Los mismos flags que usaba el post-build de CCS, para que el .hex tenga el
# formato que ya esperan las herramientas de flasheo.
$(OUT)/$(TARGET_NAME).hex: $(OUT)/$(TARGET_NAME).out
	@echo "[$(PROJECT)] HEX $(notdir $@)"
	@$(HEX) -order MS --memwidth=8 --romwidth=8 --intel -o $@ $< > /dev/null
	@echo "[$(PROJECT)] listo -> $@"

clean:
	@rm -rf $(OUT)
