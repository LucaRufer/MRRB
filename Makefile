# This Makefile is based on https://spin.atomicobject.com/2016/08/26/makefile-c-projects/
# Run configuration
TARGET ?= StreamFork
BUILD_DIR ?= ./build
INC_DIRS += ./Core
INC_DIRS += ./Drivers
INC_DIRS += ./FATFS
INC_DIRS += ./LWIP
INC_DIRS += ./Middlewares
SRC_DIRS += ./Core
SRC_DIRS += ./Drivers
SRC_DIRS += ./FATFS
SRC_DIRS += ./LWIP
SRC_DIRS += ./Middlewares
LINK_FILE := STM32H723ZGTX_FLASH.ld
# Code Generation
IOC_FILE := $(TARGET).ioc
POST_CODEGEN_SCRIPT := CodeGen/fix.sh
POST_CODEGEN_LOG := CodeGen/fix.log
# Build programs
AS = arm-none-eabi-gcc
CC = arm-none-eabi-gcc
CXX = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
OBJ_COPY = arm-none-eabi-objcopy
MAKE = make
SRCS := $(shell find $(SRC_DIRS) -name "*.cpp" -or -name "*.c" -or -name "*.s")
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
INC_DIRS_R := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS_R))
CPPFLAGS ?= $(INC_FLAGS)
# Architecture
ARCHFLAGS += -mcpu=cortex-m7 --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
CPPFLAGS += -std=gnu11 $(ARCHFLAGS)
# Errors messages
CPPFLAGS += -Wall -Wunused -Wextra -Wno-pointer-sign -Wno-unused-parameter
# Debug symbols
CPPFLAGS += -O0 -g3 -ggdb
# Definitions
CPPFLAGS += -DDEBUG -DUSE_HAL_DRIVER -DSTM32H723xx
# Flags
CPPFLAGS += -ffunction-sections -fdata-sections  -fstack-usage
# LWIP Flags
CPPFLAGS += -DLWIP_DEBUG
## Generate assembly files interleaved with c-code
CPPFLAGS +=
LDFLAGS ?= -lm -T$(LINK_FILE) $(ARCHFLAGS) -Wl,-Map="$(TARGET).map" -Wl,--gc-sections -static -Wl,--start-group -lc -lm -Wl,--end-group -Wl,--print-memory-usage
export CFLAGS
MKDIR_P ?= mkdir -p
## File Specific Targets ##
# assembly
$(BUILD_DIR)/%.s.o: %.s
	@echo "AS $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(AS) $(ASFLAGS) -c $< -o $@
# c source
$(BUILD_DIR)/%.c.o: %.c
	@echo "CC $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@  -MT $@ -MMD -MP -MF $(@:.o=.d) > $@.lst
# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "CXX $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(TARGET).elf: $(POST_CODEGEN_LOG) $(OBJS) $(LINK_FILE) Makefile
	@echo "LD $(notdir $@)"
	@$(LD) $(OBJS) -o $@ $(LDFLAGS)
compile: $(TARGET).elf
	@$(OBJ_COPY) -O binary $(TARGET).elf $(TARGET).bin
# Post-Codegen
$(POST_CODEGEN_LOG): $(IOC_FILE) $(POST_CODEGEN_SCRIPT)
	@echo "Post Codegen Fixup"
	@./$(POST_CODEGEN_SCRIPT)
.PHONY: clean all compile
all: compile
clean:
	@echo "CLEAN"
	@$(RM) -r $(BUILD_DIR)
	@$(RM) $(TARGET).elf $(TARGET).bin $(TARGET).map
	@$(RM) $(POST_CODEGEN_LOG)
-include $(DEPS)
