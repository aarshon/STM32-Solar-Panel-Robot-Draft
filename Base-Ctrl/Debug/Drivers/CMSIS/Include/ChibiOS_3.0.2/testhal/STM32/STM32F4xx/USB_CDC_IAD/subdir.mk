################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.c 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.o 

C_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/%.o Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/%.su Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/%.cyclo: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/%.c Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-testhal-2f-STM32-2f-STM32F4xx-2f-USB_CDC_IAD

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-testhal-2f-STM32-2f-STM32F4xx-2f-USB_CDC_IAD:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/main.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/testhal/STM32/STM32F4xx/USB_CDC_IAD/usbcfg.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-testhal-2f-STM32-2f-STM32F4xx-2f-USB_CDC_IAD

