VEXEL_ROOT_DIR ?= $(abspath ..)
OUT_DIR ?= $(abspath .)
DOCS_DIR ?= $(VEXEL_ROOT_DIR)/docs
EMXX ?= em++

SOURCES := \
  $(VEXEL_ROOT_DIR)/frontend/src/ast.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/backend_registry.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/codegen.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/compiler.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/evaluator.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/lexer.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/lowered_printer.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/parser.cpp \
  $(VEXEL_ROOT_DIR)/frontend/src/typechecker.cpp \
  $(VEXEL_ROOT_DIR)/backends/c/src/c_backend.cpp \
  $(VEXEL_ROOT_DIR)/backends/c/src/c_driver.cpp \
  $(VEXEL_ROOT_DIR)/backends/c/src/c_main.cpp

INCLUDES := \
  -I$(VEXEL_ROOT_DIR) \
  -I$(VEXEL_ROOT_DIR)/frontend/src \
  -I$(VEXEL_ROOT_DIR)/backends/c/src

EMXXFLAGS ?= -std=c++17 -O2 -fexceptions
EMFLAGS := \
  -s FORCE_FILESYSTEM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME='createVexelModule' \
  -s EXPORTED_RUNTIME_METHODS='["FS","callMain"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT='web' \
  -s DISABLE_EXCEPTION_CATCHING=0 \
  -Wl,--export=__main_argc_argv

TARGET := $(OUT_DIR)/vexel.js
WASM := $(OUT_DIR)/vexel.wasm
PLAYGROUND_TEMPLATE := $(OUT_DIR)/playground.template.html
PLAYGROUND := $(DOCS_DIR)/index.html

.PHONY: all clean

all: $(PLAYGROUND)

$(TARGET): $(SOURCES)
	$(EMXX) $(EMXXFLAGS) $(INCLUDES) $(SOURCES) $(EMFLAGS) -o $@

$(WASM): $(TARGET)
	@:

$(PLAYGROUND): $(PLAYGROUND_TEMPLATE) $(TARGET) $(WASM)
	@mkdir -p $(DOCS_DIR)
	python3 $(OUT_DIR)/embed.py $(PLAYGROUND_TEMPLATE) $(TARGET) $(WASM) $(PLAYGROUND)

clean:
	rm -f $(OUT_DIR)/vexel.js $(OUT_DIR)/vexel.wasm $(PLAYGROUND)
