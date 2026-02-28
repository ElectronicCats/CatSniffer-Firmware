################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Each subdirectory must supply rules for building sources it contributes
build-1216307151: ../justworks_scanner.syscfg
	@echo 'Building file: "$<"'
	@echo 'Invoking: SysConfig'
	"" --script "/Users/cldrn/workspace_ccstheia/justworks_scanner_CC1352P7_1/justworks_scanner.syscfg" -o "syscfg" --compiler ticlang
	@echo 'Finished building: "$<"'
	@echo ' '

syscfg/error.h: build-1216307151 ../justworks_scanner.syscfg
syscfg: build-1216307151
