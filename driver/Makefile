CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../build)

BACKEND_DIRS := $(filter-out common _template,$(notdir $(wildcard ../backends/*)))
BACKEND_INCLUDES := $(foreach dir,$(BACKEND_DIRS),-I../backends/$(dir)/src)

CXXFLAGS += -Isrc -I../frontend/src $(BACKEND_INCLUDES)

BIN = $(BUILD_DIR)/vexel
FRONTEND_LIB = $(BUILD_DIR)/lib/libvexelfrontend.a
BACKEND_LIBS = $(foreach dir,$(BACKEND_DIRS),$(BUILD_DIR)/backends/$(dir)/libvexel-$(dir).a)

SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/driver/%.o,$(SOURCES))

.PHONY: all clean test

all: $(BIN)

test:
	@echo "No tests defined for driver yet."

$(BIN): $(OBJECTS) $(FRONTEND_LIB) $(BACKEND_LIBS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -Wl,--start-group $(BACKEND_LIBS) $(FRONTEND_LIB) -Wl,--end-group -o $@

$(BUILD_DIR)/driver/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJECTS)
	rm -f $(OBJECTS:.o=.d)
	rm -f $(BIN)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)

-include $(OBJECTS:.o=.d)
