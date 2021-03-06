# This target makefile was automatically generated by mbedUpdater.

# Device for which the code should be built.
MBED_DEVICE        := RZ_A1H

# Can skip parsing of this makefile if user hasn't requested this device.
ifeq "$(findstring $(MBED_DEVICE),$(DEVICES))" "$(MBED_DEVICE)"

# Compiler flags which are specifc to this device.
TARGETS_FOR_DEVICE := $(BUILD_TYPE_TARGET) TARGET_UVISOR_UNSUPPORTED TARGET_A9 TARGET_CORTEX_A TARGET_LIKE_CORTEX_A9 TARGET_MBRZA1H TARGET_RENESAS TARGET_RZ_A1H
FEATURES_FOR_DEVICE := FEATURE_LWIP
PERIPHERALS_FOR_DEVICE := DEVICE_ANALOGIN DEVICE_CAN DEVICE_ERROR_PATTERN DEVICE_ETHERNET DEVICE_I2C DEVICE_I2CSLAVE DEVICE_I2C_ASYNCH DEVICE_INTERRUPTIN DEVICE_PORTIN DEVICE_PORTINOUT DEVICE_PORTOUT DEVICE_PWMOUT DEVICE_RTC DEVICE_SERIAL DEVICE_SERIAL_ASYNCH DEVICE_SERIAL_FC DEVICE_SPI DEVICE_SPISLAVE DEVICE_SPI_ASYNCH DEVICE_STDIO_MESSAGES
GCC_DEFINES := $(patsubst %,-D%,$(TARGETS_FOR_DEVICE))
GCC_DEFINES += $(patsubst %,-D%=1,$(FEATURES_FOR_DEVICE))
GCC_DEFINES += $(patsubst %,-D%=1,$(PERIPHERALS_FOR_DEVICE))
GCC_DEFINES += -D__CORTEX_A9 -DARM_MATH_CA9 -D__FPU_PRESENT -D__CMSIS_RTOS -D__EVAL -D__MBED_CMSIS_RTOS_CA9

C_FLAGS   := -mcpu=cortex-a9 -marm -mthumb-interwork  -march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard -mno-unaligned-access
ASM_FLAGS := -mcpu=cortex-a9 -marm -mthumb-interwork  -march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard -mno-unaligned-access
LD_FLAGS  := -mcpu=cortex-a9 -marm -mthumb-interwork  -march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard -mno-unaligned-access

# Extra platform specific object files to link into file binary.
DEVICE_OBJECTS := 

# Version of MRI library to use for this device.
DEVICE_MRI_LIB := 

# Determine all mbed source folders which are a match for this device so that it only needs to be done once.
DEVICE_MBED_DIRS := $(call filter_dirs,$(RAW_MBED_DIRS),$(TARGETS_FOR_DEVICE),$(FEATURES_FOR_DEVICE))

# Linker script to be used.  Indicates what should be placed where in memory.
RZ_A1H_LSCRIPT  ?= $(call find_target_linkscript,$(DEVICE_MBED_DIRS))
LSCRIPT := $(RZ_A1H_LSCRIPT)

include $(GCC4MBED_DIR)/build/device-common.mk

else
# Have an empty rule for this device since it isn't supported.
.PHONY: $(MBED_DEVICE)

ifeq "$(OS)" "Windows_NT"
$(MBED_DEVICE):
	@REM >nul
else
$(MBED_DEVICE):
	@#
endif
endif # ifeq "$(findstring $(MBED_DEVICE),$(DEVICES))"...
