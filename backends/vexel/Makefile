CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../../build)
FRONTEND_INCLUDE_DIRS := $(shell find ../../frontend/src -type d -print)

CXXFLAGS += -I../vexel/src $(addprefix -I,$(FRONTEND_INCLUDE_DIRS))

TARGET = $(BUILD_DIR)/backends/vexel/libvexel-vexel.a
CLI = $(BUILD_DIR)/vexel-vexel
FRONTEND_LIB = $(BUILD_DIR)/lib/libvexelfrontend.a
LIB_SOURCES = $(filter-out src/vexel_main.cpp,$(wildcard src/*.cpp))
LIB_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/backends/vexel/%.o,$(LIB_SOURCES))
CLI_OBJECT = $(BUILD_DIR)/backends/vexel/vexel_main.o

.PHONY: all clean test

all: $(TARGET) $(CLI)

$(TARGET): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(CLI): $(CLI_OBJECT) $(TARGET) $(FRONTEND_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CLI_OBJECT) -Wl,--start-group $(TARGET) $(FRONTEND_LIB) -Wl,--end-group -o $@

$(BUILD_DIR)/backends/vexel/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: all
	@bash tests/test.sh

clean:
	rm -f $(LIB_OBJECTS) $(CLI_OBJECT)
	rm -f $(LIB_OBJECTS:.o=.d) $(CLI_OBJECT:.o=.d)
	rm -f $(TARGET) $(CLI)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends/vexel 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR) 2>/dev/null || true

-include $(LIB_OBJECTS:.o=.d) $(CLI_OBJECT:.o=.d)
