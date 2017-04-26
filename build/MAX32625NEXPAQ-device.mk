# This target makefile was automatically generated by mbedUpdater.

# Device for which the code should be built.
MBED_DEVICE        := MAX32625NEXPAQ

# Can skip parsing of this makefile if user hasn't requested this device.
ifeq "$(findstring $(MBED_DEVICE),$(DEVICES))" "$(MBED_DEVICE)"

# Compiler flags which are specifc to this device.
TARGETS_FOR_DEVICE := $(BUILD_TYPE_TARGET) TARGET_UVISOR_UNSUPPORTED TARGET_CORTEX_M TARGET_LIKE_CORTEX_M4 TARGET_M4 TARGET_MAX32625 TARGET_MAX32625NEXPAQ TARGET_Maxim TARGET_RTOS_M4_M7
FEATURES_FOR_DEVICE :=
PERIPHERALS_FOR_DEVICE := DEVICE_ANALOGIN DEVICE_ERROR_RED DEVICE_I2C DEVICE_INTERRUPTIN DEVICE_LOWPOWERTIMER DEVICE_PORTIN DEVICE_PORTINOUT DEVICE_PORTOUT DEVICE_PWMOUT DEVICE_RTC DEVICE_SERIAL DEVICE_SERIAL_FC DEVICE_SLEEP DEVICE_SPI DEVICE_STDIO_MESSAGES
GCC_DEFINES := $(patsubst %,-D%,$(TARGETS_FOR_DEVICE))
GCC_DEFINES += $(patsubst %,-D%=1,$(FEATURES_FOR_DEVICE))
GCC_DEFINES += $(patsubst %,-D%=1,$(PERIPHERALS_FOR_DEVICE))
GCC_DEFINES += -D__CORTEX_M4 -DARM_MATH_CM4 -D__FPU_PRESENT=1 -D__CMSIS_RTOS -D__MBED_CMSIS_RTOS_CM
GCC_DEFINES += -DTARGET=MAX32625 -DTARGET_REV=0x4132 -D__SYSTEM_HFX=96000000

C_FLAGS   := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp
ASM_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp
LD_FLAGS  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp

# Extra platform specific object files to link into file binary.
DEVICE_OBJECTS := 

# Version of MRI library to use for this device.
DEVICE_MRI_LIB := 

# Determine all mbed source folders which are a match for this device so that it only needs to be done once.
DEVICE_MBED_DIRS := $(call filter_dirs,$(RAW_MBED_DIRS),$(TARGETS_FOR_DEVICE),$(FEATURES_FOR_DEVICE))

# Linker script to be used.  Indicates what should be placed where in memory.
MAX32625NEXPAQ_LSCRIPT  ?= $(call find_target_linkscript,$(DEVICE_MBED_DIRS))
LSCRIPT := $(MAX32625NEXPAQ_LSCRIPT)

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
