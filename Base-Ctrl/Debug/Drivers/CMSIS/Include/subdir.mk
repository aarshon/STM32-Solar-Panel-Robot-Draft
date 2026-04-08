################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS/Include/bldc_interface.c \
../Drivers/CMSIS/Include/bldc_interface_uart.c \
../Drivers/CMSIS/Include/buffer.c \
../Drivers/CMSIS/Include/crc.c \
../Drivers/CMSIS/Include/packet.c 

OBJS += \
./Drivers/CMSIS/Include/bldc_interface.o \
./Drivers/CMSIS/Include/bldc_interface_uart.o \
./Drivers/CMSIS/Include/buffer.o \
./Drivers/CMSIS/Include/crc.o \
./Drivers/CMSIS/Include/packet.o 

C_DEPS += \
./Drivers/CMSIS/Include/bldc_interface.d \
./Drivers/CMSIS/Include/bldc_interface_uart.d \
./Drivers/CMSIS/Include/buffer.d \
./Drivers/CMSIS/Include/crc.d \
./Drivers/CMSIS/Include/packet.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/%.o Drivers/CMSIS/Include/%.su Drivers/CMSIS/Include/%.cyclo: ../Drivers/CMSIS/Include/%.c Drivers/CMSIS/Include/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include

clean-Drivers-2f-CMSIS-2f-Include:
	-$(RM) ./Drivers/CMSIS/Include/bldc_interface.cyclo ./Drivers/CMSIS/Include/bldc_interface.d ./Drivers/CMSIS/Include/bldc_interface.o ./Drivers/CMSIS/Include/bldc_interface.su ./Drivers/CMSIS/Include/bldc_interface_uart.cyclo ./Drivers/CMSIS/Include/bldc_interface_uart.d ./Drivers/CMSIS/Include/bldc_interface_uart.o ./Drivers/CMSIS/Include/bldc_interface_uart.su ./Drivers/CMSIS/Include/buffer.cyclo ./Drivers/CMSIS/Include/buffer.d ./Drivers/CMSIS/Include/buffer.o ./Drivers/CMSIS/Include/buffer.su ./Drivers/CMSIS/Include/crc.cyclo ./Drivers/CMSIS/Include/crc.d ./Drivers/CMSIS/Include/crc.o ./Drivers/CMSIS/Include/crc.su ./Drivers/CMSIS/Include/packet.cyclo ./Drivers/CMSIS/Include/packet.d ./Drivers/CMSIS/Include/packet.o ./Drivers/CMSIS/Include/packet.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include

