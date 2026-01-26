VEXEL_ROOT_DIR ?= $(abspath .)
export VEXEL_ROOT_DIR
BUILD_DIR ?= $(abspath build)
export BUILD_DIR
CXX ?= g++
export CXX
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I$(VEXEL_ROOT_DIR)
export CXXFLAGS

.PHONY: all

all: driver

#driver
.PHONY: driver driver-test
driver: $(BUILD_DIR)/vexel

$(BUILD_DIR)/vexel: frontend backends FORCE
	+$(MAKE) -C driver

driver-test:
	+$(MAKE) -C driver test

driver-clean:
	+$(MAKE) -C driver clean

# Build frontend
.PHONY: frontend frontend-test
frontend: $(BUILD_DIR)/vexel-frontend

$(BUILD_DIR)/vexel-frontend: FORCE
	+$(MAKE) -C frontend

frontend-test:
	+$(MAKE) -C frontend test

frontend-clean:
	+$(MAKE) -C frontend clean

# Backends
BACKENDS := $(filter-out backends/common/. backends/_template/.,$(wildcard backends/*/.))
BACKENDS_TARGETS := $(patsubst backends/%/.,backend-%,$(BACKENDS))
BACKENDS_TEST_TARGETS := $(patsubst backends/%/.,backend-%-test,$(BACKENDS))
BACKENDS_CLEAN_TARGETS := $(patsubst backends/%/.,backend-%-clean,$(BACKENDS))
.PHONY: backends

backends: $(BACKENDS_TARGETS)

backend-%: $(BUILD_DIR)/vexel-%
	@:

$(BUILD_DIR)/backends/c/libvexel-c.a: FORCE
	+$(MAKE) -C backends/$(patsubst libvexel-%.a,%,$(notdir $@))

$(BUILD_DIR)/vexel-%: backends/% frontend FORCE
	+$(MAKE) -C $<

.PRECIOUS: $(BUILD_DIR)/vexel-%

backend-%-test: backends/%
	+$(MAKE) -C $< test

backend-%-clean: backends/%
	+$(MAKE) -C $< clean

	
#tests
test: driver-test frontend-test $(BACKENDS_TEST_TARGETS)

# CLEAN
clean: driver-clean frontend-clean $(BACKENDS_CLEAN_TARGETS)
	rmdir $(BUILD_DIR)

# Web playground (WASM build)
.PHONY: web
web:
	+$(MAKE) -C playground

# Force target for always-run rules
FORCE:
.PHONY: FORCE
