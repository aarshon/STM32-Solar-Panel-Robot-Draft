################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v6m.s \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v7m.s 

C_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.c \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.c 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v6m.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v7m.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.o 

S_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v6m.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v7m.d 

C_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/%.o: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/%.s Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"
Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/%.o Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/%.su Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/%.cyclo: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/%.c Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-common-2f-ports-2f-ARMCMx-2f-compilers-2f-GCC

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-common-2f-ports-2f-ARMCMx-2f-compilers-2f-GCC:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v6m.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v6m.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v7m.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt0_v7m.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/crt1.su ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.cyclo ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/common/ports/ARMCMx/compilers/GCC/vectors.su

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-common-2f-ports-2f-ARMCMx-2f-compilers-2f-GCC

