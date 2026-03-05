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
.PHONY: driver driver-test driver-clean
driver: $(BUILD_DIR)/vexel

$(BUILD_DIR)/vexel: frontend backends FORCE
	+$(MAKE) -C driver

driver-test:
	+$(MAKE) -C driver test

driver-clean:
	+$(MAKE) -C driver clean

# Build frontend
.PHONY: frontend frontend-test frontend-debug-invariants-test frontend-clean
frontend: $(BUILD_DIR)/vexel-frontend

$(BUILD_DIR)/vexel-frontend: FORCE
	+$(MAKE) -C frontend

frontend-test:
	+$(MAKE) -C frontend test

frontend-debug-invariants-test:
	+$(MAKE) -C frontend debug-invariants-test

frontend-clean:
	+$(MAKE) -C frontend clean

# Backends
BACKEND_DIRS := $(filter-out backends/ext/ backends/tests/,$(wildcard backends/*/ backends/ext/*/))
BACKEND_NAME_LIST := $(notdir $(patsubst %/,%,$(BACKEND_DIRS)))
BACKEND_NAMES := $(sort $(BACKEND_NAME_LIST))
DUP_BACKEND_NAMES := $(sort $(foreach n,$(BACKEND_NAMES),$(if $(filter-out 1,$(words $(filter $(n),$(BACKEND_NAME_LIST)))),$(n),)))

ifneq ($(strip $(DUP_BACKEND_NAMES)),)
$(error Duplicate backend names detected: $(DUP_BACKEND_NAMES). Ensure each backend directory basename is unique across backends/ and backends/ext/)
endif

BACKENDS_TARGETS := $(addprefix backend-,$(BACKEND_NAMES))
BACKENDS_CLEAN_TARGETS := $(addprefix backend-,$(addsuffix -clean,$(BACKEND_NAMES)))

backend_dir = $(firstword $(filter %/$1/,$(BACKEND_DIRS)))
.PHONY: backends $(BACKENDS_TARGETS) $(BACKENDS_CLEAN_TARGETS) backend-conformance-test

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

	
backend-conformance-test:
	@bash backends/conformance_test.sh

#tests
.PHONY: backend-conformance-test docs-check test clean web
docs-check:
	@index="docs/index.html"; \
	search_cmd() { \
		if command -v rg >/dev/null 2>&1; then rg -q "$$1" "$$2"; else grep -E -q "$$1" "$$2"; fi; \
	}; \
	if [ ! -f "$$index" ]; then \
		echo "docs/index.html not found" >&2; \
		exit 1; \
	fi; \
	if search_cmd "VEXEL_WASM_BASE64|createVexelModule|wasmBinaryFile=\"vexel.wasm\"|/\\*__VEXEL_JS__\\*/" "$$index"; then \
		echo "docs/index.html appears to contain generated playground payload" >&2; \
		exit 1; \
	fi; \
	if ! search_cmd "playground\\.html" "$$index"; then \
		echo "docs/index.html must link to playground.html" >&2; \
		exit 1; \
	fi; \
	echo "ok"

test: driver-test frontend-test backend-conformance-test docs-check

# CLEAN
clean: driver-clean frontend-clean $(BACKENDS_CLEAN_TARGETS)
	find . -type f -name 'out.analysis.txt' -delete
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR) 2>/dev/null || true

# Web playground (WASM build)
.PHONY: web
web:
	+$(MAKE) -C playground

# Force target for always-run rules
FORCE:
.PHONY: FORCE
