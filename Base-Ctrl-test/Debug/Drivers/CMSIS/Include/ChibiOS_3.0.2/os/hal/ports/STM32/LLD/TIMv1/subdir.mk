################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.c 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.o 

C_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/%.o Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/%.su Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/%.cyclo: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/%.c Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-hal-2f-ports-2f-STM32-2f-LLD-2f-TIMv1

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-hal-2f-ports-2f-STM32-2f-LLD-2f-TIMv1:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/gpt_lld.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/icu_lld.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/pwm_lld.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/hal/ports/STM32/LLD/TIMv1/st_lld.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-hal-2f-ports-2f-STM32-2f-LLD-2f-TIMv1

