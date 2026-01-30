################################################################################
# Automatically-generated file. Do not edit!
################################################################################

GEN_OPTS__FLAG :=
GEN_CMDS__FLAG :=

ORDERED_OBJS += \
$(GEN_CMDS__FLAG) \
ti_utils_build_linker.cmd.genlibs \
libc.a \

-include ../makefile.init

RM :=
RMDIR :=

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir_vars.mk
-include subdir_rules.mk
-include objects.mk

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables

# All Target
all: justworks_scanner_CC1352P7_1.out

# Tool invocations
justworks_scanner_CC1352P7_1.out: $(OBJS) $(USER_OBJS)
	@echo 'Building target: "$@"'
	@echo 'Invoking: Arm Linker'
	justworks_scanner_CC1352P7_1.out justworks_scanner_CC1352P7_1.map justworks_scanner_CC1352P7_1_linkInfo.xml /source /kernel/tirtos7/packages /syscfg /lib 16002-D 10247-D 10325-D 10229-D 16032-D FLASH_ROM_BUILD=2  "justworks_scanner_CC1352P7_1.out" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: "$@"'
	@echo ' '
	@$(MAKE) --no-print-directory post-build

# Other Targets
clean:
	-$(RM) $(GEN_MISC_FILES__QUOTED)
	-$(RMDIR) $(GEN_MISC_DIRS__QUOTED)
	-@echo 'Finished clean'
	-@echo ' '

post-build:
	-/bin/tiarmobjcopy -O ihex justworks_scanner_CC1352P7_1.out justworks_scanner_CC1352P7_1.hex
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
