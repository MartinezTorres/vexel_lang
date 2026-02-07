CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../../build)
FRONTEND_INCLUDE_DIRS := $(shell find ../../frontend/src -type d -print)
COMMON_DIR ?= ../common
COMMON_INCLUDE_DIR := $(COMMON_DIR)/src
COMMON_LIB := $(BUILD_DIR)/backends/common/libvexel-backend-common.a

CXXFLAGS += -I../c/src -I$(COMMON_INCLUDE_DIR) $(addprefix -I,$(FRONTEND_INCLUDE_DIRS))

TARGET = $(BUILD_DIR)/backends/c/libvexel-c.a
CLI = $(BUILD_DIR)/vexel-c
FRONTEND_LIB = $(BUILD_DIR)/lib/libvexelfrontend.a
LIB_SOURCES = $(filter-out src/c_main.cpp,$(wildcard src/*.cpp))
LIB_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/backends/c/%.o,$(LIB_SOURCES))
CLI_OBJECT = $(BUILD_DIR)/backends/c/c_main.o

.PHONY: all clean cli test

all: $(TARGET) $(CLI)

$(TARGET): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(CLI): $(CLI_OBJECT) $(TARGET) $(FRONTEND_LIB) $(COMMON_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CLI_OBJECT) -Wl,--start-group $(TARGET) $(COMMON_LIB) $(FRONTEND_LIB) -Wl,--end-group -o $@

$(COMMON_LIB):
	+$(MAKE) -C $(COMMON_DIR)

$(BUILD_DIR)/backends/c/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: all
	@python3 tests/run_tests.py

clean:
	rm -f $(LIB_OBJECTS) $(CLI_OBJECT)
	rm -f $(LIB_OBJECTS:.o=.d) $(CLI_OBJECT:.o=.d)
	rm -f $(TARGET) $(CLI)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends/c
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)

-include $(LIB_OBJECTS:.o=.d) $(CLI_OBJECT:.o=.d)
