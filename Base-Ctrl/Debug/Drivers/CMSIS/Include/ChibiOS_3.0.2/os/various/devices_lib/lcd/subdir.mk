################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.c 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.o 

C_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/%.o Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/%.su Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/%.cyclo: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/%.c Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-various-2f-devices_lib-2f-lcd

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-various-2f-devices_lib-2f-lcd:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/various/devices_lib/lcd/lcd3310.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-various-2f-devices_lib-2f-lcd

