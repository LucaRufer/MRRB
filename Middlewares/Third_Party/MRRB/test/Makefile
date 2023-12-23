# Run configuration
TARGET ?= Test
PROJECT_DIR ?= ../..
BUILD_DIR ?= ./build
COVERAGE_DIR ?= ./coverage
INC_DIRS += MRRB
INC_DIRS += MRRB/test
INC_DIRS += Unity/src
SRC_DIRS += MRRB
SRC_DIRS += Unity/src
COV_DIRS += MRRB
# Build programs
AS = gcc
CC = gcc
CXX = gcc
LD = gcc
COV = gcov
OBJ_COPY = gcc-objcopy
MOVE = mv
MKDIR_P = mkdir -p
# Add project directory as prefix
INC_DIRS := $(addprefix $(PROJECT_DIR)/,$(INC_DIRS))
SRC_DIRS := $(addprefix $(PROJECT_DIR)/,$(SRC_DIRS))
COV_DIRS := $(addprefix $(PROJECT_DIR)/,$(COV_DIRS))
# Find source files
SRCS := $(shell find $(SRC_DIRS) -name "*.cpp" -or -name "*.c" -or -name "*.s")
OBJS := $(patsubst $(PROJECT_DIR)/%, $(BUILD_DIR)/%, $(SRCS:%=%.o))
DEPS := $(OBJS:.o=.d)
# Coverage
COV_SRCS := $(shell find $(COV_DIRS) -name "*.cpp" -or -name "*.c" -or -name "*.s")
COVS := $(patsubst $(PROJECT_DIR)/%, $(COVERAGE_DIR)/%, $(COV_SRCS:%=%.gcov))
# Proprocessor Macros
DEFS += TEST
DEFS += MRRB_ALLOW_WRITE_FROM_ISR=0 MRRB_USE_OS=1 MRRB_SYSTEM=MRRB_SYSTEM_UNIX
DEFS += MRRB_PORT_PATH=\"port.h\"
DEFS += UNITY_INCLUDE_CONFIG_H
# Include flags
CPPFLAGS += $(addprefix -I,$(INC_DIRS))
# Architecture
ARCHFLAGS ?=
CPPFLAGS += -std=gnu11 $(ARCHFLAGS)
# Errors messages
CPPFLAGS += -Wall -Wunused -Wextra -Wno-pointer-sign -Wno-unused-parameter
# Optimizations and Debug symbols
CPPFLAGS += -O0 -g3 -ggdb
# Preprocessor Macros
CPPFLAGS += $(addprefix -D,$(DEFS))
# Coverage
CPPFLAGS += --coverage
# Coverage
LDFLAGS += --coverage
export CFLAGS
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
$(TARGET): $(OBJS) Makefile
	@echo "LD $(notdir $@)"
	@$(LD) $(OBJS) -o $@ $(LDFLAGS)
# Coverage Reports
$(COVERAGE_DIR)/%.gcov:
	@$(MKDIR_P) $(dir $@)
	@$(COV) $@ -o $(BUILD_DIR)/$*.gcdno >> $(COVERAGE_DIR)/$(COV).log
	@$(MOVE) *.gcov $(dir $@) 2> /dev/null | true
# Coverage Log
$(COVERAGE_DIR)/$(COV).log:
	@$(MKDIR_P) $(COVERAGE_DIR)
	@echo "Test Coverage Log $(shell date)" > $(COVERAGE_DIR)/$(COV).log

.PHONY: all clean compile coverage run
# Other
all: compile
compile: $(TARGET)
run: compile
	@./$(TARGET)
coverage: clean $(COVERAGE_DIR)/$(COV).log run $(COVS)
clean:
	@echo "CLEAN"
	@$(RM) -r $(BUILD_DIR)
	@$(RM) -r $(COVERAGE_DIR)
	@$(RM) $(TARGET) $(TARGET).map
-include $(DEPS)
