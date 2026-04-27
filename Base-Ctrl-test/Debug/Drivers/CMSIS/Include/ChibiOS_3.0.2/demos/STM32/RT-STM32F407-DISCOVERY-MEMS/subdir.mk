################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.c 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.o 

C_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/%.o Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/%.su Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/%.cyclo: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/%.c Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-demos-2f-STM32-2f-RT-2d-STM32F407-2d-DISCOVERY-2d-MEMS

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-demos-2f-STM32-2f-RT-2d-STM32F407-2d-DISCOVERY-2d-MEMS:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/main.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/demos/STM32/RT-STM32F407-DISCOVERY-MEMS/usbcfg.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-demos-2f-STM32-2f-RT-2d-STM32F407-2d-DISCOVERY-2d-MEMS

