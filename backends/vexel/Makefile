CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../../build)
FRONTEND_INCLUDE_DIRS := $(shell find ../../frontend/src -type d -print)

CXXFLAGS += -I../vexel/src $(addprefix -I,$(FRONTEND_INCLUDE_DIRS))

TARGET = $(BUILD_DIR)/backends/vexel/libvexel-vexel.a
LIB_SOURCES = $(wildcard src/*.cpp)
LIB_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/backends/vexel/%.o,$(LIB_SOURCES))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(BUILD_DIR)/backends/vexel/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: all
	@bash tests/test.sh

clean:
	rm -f $(LIB_OBJECTS)
	rm -f $(LIB_OBJECTS:.o=.d)
	rm -f $(TARGET)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends/vexel 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR) 2>/dev/null || true

-include $(LIB_OBJECTS:.o=.d)
