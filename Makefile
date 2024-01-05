# This Makefile is based on https://spin.atomicobject.com/2016/08/26/makefile-c-projects/
# Run configuration
TARGET ?= MRRB_Example
PROJECT_DIR ?= .
BUILD_DIR ?= ./build
INC_DIRS += Core
INC_DIRS += Drivers
INC_DIRS += FATFS
INC_DIRS += LWIP
INC_DIRS += Middlewares
SRC_DIRS += Core
SRC_DIRS += Drivers
SRC_DIRS += FATFS
SRC_DIRS += LWIP
SRC_DIRS += Middlewares
EXCLUDE_SRC_DIRS += Middlewares/Third_Party/MRRB/test
LINK_FILE ?= STM32H723ZGTX_FLASH.ld
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
MKDIR_P = mkdir -p
# Add project directory as prefix
INC_DIRS := $(addprefix $(PROJECT_DIR)/,$(INC_DIRS))
SRC_DIRS := $(addprefix $(PROJECT_DIR)/,$(SRC_DIRS))
EXCLUDE_SRC_DIRS := $(addprefix $(PROJECT_DIR)/,$(EXCLUDE_SRC_DIRS))
# Find source files and filter excluded
SRCS := $(shell find $(SRC_DIRS) -name "*.cpp" -or -name "*.c" -or -name "*.s")
EXLUCDE_SRCS := $(shell find $(EXCLUDE_SRC_DIRS) -name "*.cpp" -or -name "*.c" -or -name "*.s")
SRCS := $(filter-out $(EXLUCDE_SRCS),$(SRCS))
OBJS := $(patsubst $(PROJECT_DIR)/%, $(BUILD_DIR)/%, $(SRCS:%=%.o))
DEPS := $(OBJS:.o=.d)
# Includes
INCS := $(shell find $(INC_DIRS) -type d)
# Proprocessor Macros
DEFS += DEBUG
DEFS += USE_HAL_DRIVER STM32H723xx
DEFS += LWIP_DEBUG
# Used symbols
USED_SYMBOLS += uxTopUsedPriority
# C and C++ flags
CPPFLAGS += $(addprefix -I,$(INCS))
# Architecture
ARCHFLAGS += -mcpu=cortex-m7 --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
CPPFLAGS += -std=gnu11 $(ARCHFLAGS)
# Errors messages
CPPFLAGS += -Wall -Wunused -Wextra -Wno-pointer-sign -Wno-unused-parameter
# Optimizations and Debug symbols
CPPFLAGS += -O0 -g3 -ggdb
# Preprocessor Macros
CPPFLAGS += $(addprefix -D,$(DEFS))
# Used Symbols
CPPFLAGS += $(addprefix -u=,$(USED_SYMBOLS))
# Flags
CPPFLAGS += -ffunction-sections -fdata-sections  -fstack-usage
## Linker Options ##
# Static linking
LDFLAGS += -static
# Link math library
LDFLAGS += -lm
# Specify linker file
LDFLAGS += -T$(LINK_FILE)
# Add Architecture Flags
LDFLAGS += $(ARCHFLAGS)
# Create link map file
LDFLAGS += -Wl,-Map="$(TARGET).map"
# Remove unused symbols, except for specified ones
LDFLAGS += -Wl,--gc-sections $(addprefix -Xlinker -undefined=,$(USED_SYMBOLS))
# Prevent problems in circular dependencies of c and m libraries
LDFLAGS += -Wl,--start-group -lc -lm -Wl,--end-group
# Print memory usage after linking
LDFLAGS += -Wl,--print-memory-usage
## File Specific Targets ##
# assembly
$(BUILD_DIR)/%.s.o: $(PROJECT_DIR)/%.s Makefile
	@echo "AS $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(AS) $(ASFLAGS) -c $< -o $@
# c source
$(BUILD_DIR)/%.c.o: $(PROJECT_DIR)/%.c Makefile
	@echo "CC $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@  -MT $@ -MMD -MP -MF $(@:.o=.d) > $@.lst
# c++ source
$(BUILD_DIR)/%.cpp.o: $(PROJECT_DIR)/%.cpp Makefile
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
.PHONY: all clean compile
all: compile
clean:
	@echo "CLEAN"
	@$(RM) -r $(BUILD_DIR)
	@$(RM) $(TARGET).elf $(TARGET).bin $(TARGET).map
	@$(RM) $(POST_CODEGEN_LOG)
-include $(DEPS)
