################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v6m.s \
../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.s 

OBJS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v6m.o \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.o 

S_DEPS += \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v6m.d \
./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/%.o: ../Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/%.s Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-rt-2f-ports-2f-ARMCMx-2f-compilers-2f-GCC

clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-rt-2f-ports-2f-ARMCMx-2f-compilers-2f-GCC:
	-$(RM) ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v6m.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v6m.o ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.d ./Drivers/CMSIS/Include/ChibiOS_3.0.2/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.o

.PHONY: clean-Drivers-2f-CMSIS-2f-Include-2f-ChibiOS_3-2e-0-2e-2-2f-os-2f-rt-2f-ports-2f-ARMCMx-2f-compilers-2f-GCC

