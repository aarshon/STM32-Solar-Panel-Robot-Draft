################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.c 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.o 

C_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/%.o Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/%.su Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/%.cyclo: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/%.c Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-ext-2f-stdperiph_stm32f4-2f-src

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-ext-2f-stdperiph_stm32f4-2f-src:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/misc.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_adc.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_dma.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_exti.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_flash.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_rcc.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_syscfg.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_tim.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/ext/stdperiph_stm32f4/src/stm32f4xx_wwdg.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-ext-2f-stdperiph_stm32f4-2f-src

