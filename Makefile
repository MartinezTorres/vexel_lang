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
BACKEND_DIRS := $(filter-out backends/common/ backends/_template/ backends/ext/,$(wildcard backends/*/ backends/ext/*/))
BACKEND_NAMES := $(sort $(notdir $(patsubst %/,%,$(BACKEND_DIRS))))
BACKENDS_TARGETS := $(addprefix backend-,$(BACKEND_NAMES))
BACKENDS_TEST_TARGETS := $(addprefix backend-,$(addsuffix -test,$(BACKEND_NAMES)))
BACKENDS_CLEAN_TARGETS := $(addprefix backend-,$(addsuffix -clean,$(BACKEND_NAMES)))

backend_dir = $(firstword $(filter %/$1/,$(BACKEND_DIRS)))
.PHONY: backends

backends: $(BACKENDS_TARGETS)

backend-%: frontend FORCE
	@dir="$(call backend_dir,$*)"; \
	if [ -z "$$dir" ]; then echo "Unknown backend '$*'"; exit 1; fi; \
	$(MAKE) -C "$$dir"

backend-%-test: FORCE
	@dir="$(call backend_dir,$*)"; \
	if [ -z "$$dir" ]; then echo "Unknown backend '$*'"; exit 1; fi; \
	$(MAKE) -C "$$dir" test

backend-%-clean: FORCE
	@dir="$(call backend_dir,$*)"; \
	if [ -z "$$dir" ]; then echo "Unknown backend '$*'"; exit 1; fi; \
	$(MAKE) -C "$$dir" clean

	
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
