# Run configuration
TARGET ?= lib
PROJECT_DIR ?= ../..
BUILD_DIR ?= ./build
INC_DIRS += .
INC_DIRS += ./test/lib/include
SRC_DIRS += .
# Build programs
override AS ?= gcc
override CC ?= gcc
override CXX ?= gcc
override LD ?= gcc
override AR ?= ar
MKDIR_P ?= mkdir -p
# Add project directory as prefix
INC_DIRS := $(addprefix $(PROJECT_DIR)/,$(INC_DIRS))
SRC_DIRS := $(addprefix $(PROJECT_DIR)/,$(SRC_DIRS))
# Find source files
SRCS := $(wildcard $(SRC_DIRS)/*.c)
OBJS := $(patsubst $(PROJECT_DIR)/%, $(BUILD_DIR)/%, $(SRCS:%=%.o))
DEPS := $(OBJS:.o=.d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
# Proprocessor Macros
DEFS ?= MRRB_SYSTEM=MRRB_SYSTEM_UNIX MRRB_ALLOW_WRITE_FROM_ISR=0 MRRB_USE_OS=1
# Definitions
CPPFLAGS += $(INC_FLAGS)
# Architecture
ARCHFLAGS ?=
CPPFLAGS += -std=gnu11 $(ARCHFLAGS)
# Errors messages
CPPFLAGS += -Wall -Wunused -Wextra -Wno-pointer-sign -Wno-unused-parameter
# Optimizations and Debug symbols
CPPFLAGS += -O0 -g3 -ggdb
# Preprocessor Macros
CPPFLAGS += $(addprefix -D,$(DEFS))
# Linker Flags
LDFLAGS ?=
## File Specific Targets ##
# assembly
$(BUILD_DIR)/%.s.o: $(PROJECT_DIR)/%.s
	@echo "AS $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(AS) $(ASFLAGS) -c $< -o $@
# c source
$(BUILD_DIR)/%.c.o: $(PROJECT_DIR)/%.c
	@echo "CC $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@  -MT $@ -MMD -MP -MF $(@:.o=.d) > $@.lst
# c++ source
$(BUILD_DIR)/%.cpp.o: $(PROJECT_DIR)/%.cpp
	@echo "CXX $(notdir $@)"
	@$(MKDIR_P) $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
# Target
$(TARGET).a: $(OBJS) Makefile
	@echo "AR $(notdir $@)"
	@$(AR) -rcs $@ $(OBJS)

.PHONY: all lib clean
# Other
all: lib
lib: $(TARGET).a
clean:
	@echo "CLEAN"
	@$(RM) -r $(BUILD_DIR)
-include $(DEPS)
