CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../../build)
FRONTEND_INCLUDE_DIRS := $(shell find ../../frontend/src -type d -print)

CXXFLAGS += -Isrc $(addprefix -I,$(FRONTEND_INCLUDE_DIRS))

TARGET = $(BUILD_DIR)/backends/common/libvexel-backend-common.a
SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/backends/common/%.o,$(SOURCES))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(BUILD_DIR)/backends/common/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test:
	@echo "No tests defined for backend common yet."

clean:
	rm -f $(OBJECTS)
	rm -f $(OBJECTS:.o=.d)
	rm -f $(TARGET)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends/common
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)

-include $(OBJECTS:.o=.d)
