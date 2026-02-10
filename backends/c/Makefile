CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../../build)
FRONTEND_INCLUDE_DIRS := $(shell find ../../frontend/src -type d -print)

CXXFLAGS += -I../c/src $(addprefix -I,$(FRONTEND_INCLUDE_DIRS))

TARGET = $(BUILD_DIR)/backends/c/libvexel-c.a
LIB_SOURCES = $(wildcard src/*.cpp)
LIB_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/backends/c/%.o,$(LIB_SOURCES))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(BUILD_DIR)/backends/c/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: all
	@python3 tests/run_tests.py

clean:
	rm -f $(LIB_OBJECTS)
	rm -f $(LIB_OBJECTS:.o=.d)
	rm -f $(TARGET)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends/c 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR) 2>/dev/null || true

-include $(LIB_OBJECTS:.o=.d)
