CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../../build)

CXXFLAGS += -I../banked/src -I../../frontend/src

TARGET = $(BUILD_DIR)/backends/banked/libvexel-banked.a
CLI = $(BUILD_DIR)/vexel-banked
FRONTEND_LIB = $(BUILD_DIR)/lib/libvexelfrontend.a

LIB_SOURCES = $(filter-out src/banked_main.cpp,$(wildcard src/*.cpp))
LIB_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/backends/banked/%.o,$(LIB_SOURCES))
CLI_OBJECT = $(BUILD_DIR)/backends/banked/banked_main.o

.PHONY: all clean cli test

all: $(TARGET) $(CLI)

$(TARGET): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(CLI): $(CLI_OBJECT) $(TARGET) $(FRONTEND_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CLI_OBJECT) -Wl,--start-group $(TARGET) $(FRONTEND_LIB) -Wl,--end-group -o $@

$(BUILD_DIR)/backends/banked/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test:
	@echo "No tests defined for backend 'banked' yet."

clean:
	rm -f $(LIB_OBJECTS) $(CLI_OBJECT)
	rm -f $(LIB_OBJECTS:.o=.d) $(CLI_OBJECT:.o=.d)
	rm -f $(TARGET) $(CLI)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends/banked
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/backends
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)

-include $(LIB_OBJECTS:.o=.d) $(CLI_OBJECT:.o=.d)
