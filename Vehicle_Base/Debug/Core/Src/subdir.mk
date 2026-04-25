################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/battery.c \
../Core/Src/bldc_interface.c \
../Core/Src/bldc_interface_uart.c \
../Core/Src/buffer.c \
../Core/Src/comm_protocol.c \
../Core/Src/crc.c \
../Core/Src/estop.c \
../Core/Src/keypad.c \
../Core/Src/lcd.c \
../Core/Src/main.c \
../Core/Src/oled.c \
../Core/Src/packet.c \
../Core/Src/screen_estop.c \
../Core/Src/screen_power.c \
../Core/Src/ssd1306.c \
../Core/Src/ssd1306_fonts.c \
../Core/Src/stm32f7xx_hal_msp.c \
../Core/Src/stm32f7xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f7xx.c \
../Core/Src/ui.c \
../Core/Src/vesc.c 

OBJS += \
./Core/Src/battery.o \
./Core/Src/bldc_interface.o \
./Core/Src/bldc_interface_uart.o \
./Core/Src/buffer.o \
./Core/Src/comm_protocol.o \
./Core/Src/crc.o \
./Core/Src/estop.o \
./Core/Src/keypad.o \
./Core/Src/lcd.o \
./Core/Src/main.o \
./Core/Src/oled.o \
./Core/Src/packet.o \
./Core/Src/screen_estop.o \
./Core/Src/screen_power.o \
./Core/Src/ssd1306.o \
./Core/Src/ssd1306_fonts.o \
./Core/Src/stm32f7xx_hal_msp.o \
./Core/Src/stm32f7xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f7xx.o \
./Core/Src/ui.o \
./Core/Src/vesc.o 

C_DEPS += \
./Core/Src/battery.d \
./Core/Src/bldc_interface.d \
./Core/Src/bldc_interface_uart.d \
./Core/Src/buffer.d \
./Core/Src/comm_protocol.d \
./Core/Src/crc.d \
./Core/Src/estop.d \
./Core/Src/keypad.d \
./Core/Src/lcd.d \
./Core/Src/main.d \
./Core/Src/oled.d \
./Core/Src/packet.d \
./Core/Src/screen_estop.d \
./Core/Src/screen_power.d \
./Core/Src/ssd1306.d \
./Core/Src/ssd1306_fonts.d \
./Core/Src/stm32f7xx_hal_msp.d \
./Core/Src/stm32f7xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f7xx.d \
./Core/Src/ui.d \
./Core/Src/vesc.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/battery.cyclo ./Core/Src/battery.d ./Core/Src/battery.o ./Core/Src/battery.su ./Core/Src/bldc_interface.cyclo ./Core/Src/bldc_interface.d ./Core/Src/bldc_interface.o ./Core/Src/bldc_interface.su ./Core/Src/bldc_interface_uart.cyclo ./Core/Src/bldc_interface_uart.d ./Core/Src/bldc_interface_uart.o ./Core/Src/bldc_interface_uart.su ./Core/Src/buffer.cyclo ./Core/Src/buffer.d ./Core/Src/buffer.o ./Core/Src/buffer.su ./Core/Src/comm_protocol.cyclo ./Core/Src/comm_protocol.d ./Core/Src/comm_protocol.o ./Core/Src/comm_protocol.su ./Core/Src/crc.cyclo ./Core/Src/crc.d ./Core/Src/crc.o ./Core/Src/crc.su ./Core/Src/estop.cyclo ./Core/Src/estop.d ./Core/Src/estop.o ./Core/Src/estop.su ./Core/Src/keypad.cyclo ./Core/Src/keypad.d ./Core/Src/keypad.o ./Core/Src/keypad.su ./Core/Src/lcd.cyclo ./Core/Src/lcd.d ./Core/Src/lcd.o ./Core/Src/lcd.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/oled.cyclo ./Core/Src/oled.d ./Core/Src/oled.o ./Core/Src/oled.su ./Core/Src/packet.cyclo ./Core/Src/packet.d ./Core/Src/packet.o ./Core/Src/packet.su ./Core/Src/screen_estop.cyclo ./Core/Src/screen_estop.d ./Core/Src/screen_estop.o ./Core/Src/screen_estop.su ./Core/Src/screen_power.cyclo ./Core/Src/screen_power.d ./Core/Src/screen_power.o ./Core/Src/screen_power.su ./Core/Src/ssd1306.cyclo ./Core/Src/ssd1306.d ./Core/Src/ssd1306.o ./Core/Src/ssd1306.su ./Core/Src/ssd1306_fonts.cyclo ./Core/Src/ssd1306_fonts.d ./Core/Src/ssd1306_fonts.o ./Core/Src/ssd1306_fonts.su ./Core/Src/stm32f7xx_hal_msp.cyclo ./Core/Src/stm32f7xx_hal_msp.d ./Core/Src/stm32f7xx_hal_msp.o ./Core/Src/stm32f7xx_hal_msp.su ./Core/Src/stm32f7xx_it.cyclo ./Core/Src/stm32f7xx_it.d ./Core/Src/stm32f7xx_it.o ./Core/Src/stm32f7xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f7xx.cyclo ./Core/Src/system_stm32f7xx.d ./Core/Src/system_stm32f7xx.o ./Core/Src/system_stm32f7xx.su ./Core/Src/ui.cyclo ./Core/Src/ui.d ./Core/Src/ui.o ./Core/Src/ui.su ./Core/Src/vesc.cyclo ./Core/Src/vesc.d ./Core/Src/vesc.o ./Core/Src/vesc.su

.PHONY: clean-Core-2f-Src

