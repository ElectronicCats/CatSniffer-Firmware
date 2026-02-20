SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR ?= $(HOME)/ti/simplelink_cc13xx_cc26xx_sdk_7_41_00_17
SYSCONFIG_TOOL = $(HOME)/ti/sysconfig_1.18.1/sysconfig_cli.sh
GCC_ARMCOMPILER = $(HOME)/ti/gcc_arm_none_eabi_9_2_1

include $(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/imports.mak

CC = "$(GCC_ARMCOMPILER)/bin/arm-none-eabi-gcc"
LNK = "$(GCC_ARMCOMPILER)/bin/arm-none-eabi-gcc"
HEX = "$(GCC_ARMCOMPILER)/bin/arm-none-eabi-objcopy"

OBJECTS = main.obj uart_proto.obj capture.obj radio.obj smartrf_settings.obj ti_drivers_config.obj ti_devices_config.obj startup_gcc.obj

NAME = raw_repeater

CFLAGS = -I. \
    -DDeviceFamily_CC13X2X7 \
    -DDRIVERLIB_NOROM \
    -I$(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/source \
    -I$(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/source/ti/drivers/rf \
    -I$(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/kernel/nortos \
    -I$(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/kernel/nortos/posix \
    -mcpu=cortex-m4 \
    -march=armv7e-m \
    -mthumb \
    -std=c99 \
    -mfloat-abi=hard \
    -mfpu=fpv4-sp-d16 \
    -ffunction-sections \
    -fdata-sections \
    -g \
    -gstrict-dwarf \
    -Wall \
    "-I$(GCC_ARMCOMPILER)/arm-none-eabi/include/newlib-nano" \
    "-I$(GCC_ARMCOMPILER)/arm-none-eabi/include"

LFLAGS = -Wl,-T,cc13x2x7_cc26x2x7_nortos.lds \
    "-Wl,-Map,$(NAME).map" \
    "-L$(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/source" \
    "-L$(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/kernel/nortos" \
    -l:ti/drivers/lib/gcc/m4f/drivers_cc13x2x7.a \
    -l:ti/drivers/rf/lib/gcc/m4f/rf_multiMode_cc13x2.a \
    -l:ti/devices/cc13x2x7_cc26x2x7/driverlib/bin/gcc/driverlib.lib \
    -l:lib/gcc/m4f/nortos_cc13x2x7.a \
    -march=armv7e-m \
    -mthumb \
    -mfloat-abi=hard \
    -mfpu=fpv4-sp-d16 \
    -nostartfiles \
    -static \
    -Wl,--gc-sections \
    -lgcc \
    -lc \
    -lm \
    --specs=nano.specs

all: $(NAME).elf

$(NAME).elf: $(OBJECTS)
	@ echo Linking $@
	@ $(LNK)  $(OBJECTS)  $(LFLAGS) -o $(NAME).elf
	@ $(HEX) -O ihex $(NAME).elf $(NAME).hex
	@ mkdir -p build
	@ mv $(NAME).hex build/
	@ mv $(NAME).elf build/
	@ $(RM) $(OBJECTS) $(NAME).map ti_drivers_config.obj ti_devices_config.obj

ti_drivers_config.c ti_drivers_config.h ti_devices_config.c ti_devices_config.h: $(NAME).syscfg
	@ echo "SysConfig $(NAME).syscfg"
	@ $(SYSCONFIG_TOOL) --product $(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/.metadata/product.json --output . --compiler gcc --board /ti/boards/LP_CC1352P7_1 --rtos nortos $(NAME).syscfg

%.obj: %.c ti_drivers_config.h
	@ echo Building $@
	@ $(CC) $(CFLAGS) $< -c -o $@

clean:
	@ echo Cleaning...
	@ $(RM) $(OBJECTS) $(NAME).map build/$(NAME).hex build/$(NAME).elf ti_drivers_config.c ti_drivers_config.h ti_utils_build_linker.cmd.genlibs ti_drivers_config.obj ti_devices_config.c ti_devices_config.obj syscfg_c.rov.xs ti_utils_build_compiler.opt
	@ rm -rf build


startup_gcc.obj: $(SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)/source/ti/devices/cc13x2x7_cc26x2x7/startup_files/startup_gcc.c
	@ echo Building $@
	@ $(CC) $(CFLAGS) $< -c -o $@
