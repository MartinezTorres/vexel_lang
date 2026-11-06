SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DELETE_ON_ERROR:
SDCC ?= sdcc
AS ?= sdasz80
# GNU make predefines AS=as; the cartridge needs SDCC's Z80 assembler.
ifeq ($(origin AS),default)
AS := sdasz80
endif
CXX ?= c++
PYTHON ?= python3
BUILD ?= build
SDCCFLAGS ?= -mz80 --std-sdcc11 --sdcccall 0 --opt-code-size
CPPFLAGS += -Igenerated -Iruntime
MEGALINKER ?= $(BUILD)/megalinker
LIBS ?=
SIMFLAGS ?=
BRIDGE_SOURCE ?= generated/vx_bridges.c
RUNNER_CXXFLAGS ?= -O2 -Wall -Wextra
include generated/modules.mk
-include local.mk

# A user module overrides exactly one generated module, preserving its ABI.
# Assembly wins over C if both exist. Other user modules are resident by default.
source = $(firstword $(wildcard user/$(1).s user/$(1).c generated/$(1).c))
SELECTED := $(foreach m,$(BANK_MODULES),$(call source,$(m)))
USER_SOURCES := $(filter-out $(SELECTED),$(wildcard user/*.c user/*.s))
USER_RELS := $(addprefix $(BUILD)/user_,$(addsuffix .rel,$(basename $(notdir $(USER_SOURCES)))))
OBJECTS := $(BUILD)/crt0.rel $(BUILD)/platform.rel $(BUILD)/vx_runtime.rel \
           $(BUILD)/vx_bridges.rel $(addprefix $(BUILD)/,$(addsuffix .rel,$(BANK_MODULES))) $(USER_RELS)
ifeq ($(FIXED_RUNTIME),1)
OBJECTS += $(BUILD)/vx_fixed.rel
endif
ifeq ($(FLOAT_RUNTIME),1)
OBJECTS += $(BUILD)/vx_float.rel
endif
ifeq ($(MATH_RUNTIME),1)
OBJECTS += $(BUILD)/vx_math.rel
endif
ifneq ($(MAPPER_SEGMENTS),0)
OBJECTS += $(BUILD)/vx_mapper.rel $(BUILD)/mapper.rel
endif
HEADERS := $(wildcard generated/*.h runtime/*.h user/*.h)

.PHONY: all check simulate clean FORCE
all: $(BUILD)/game.rom
check: $(BUILD)/game.rom
simulate: $(BUILD)/game.rom $(BUILD)/neo8-runner
	@$(RM) $(BUILD)/execution.json
	$(PYTHON) tools/check_project.py generated/plan.json $(BUILD)/game.rom $(BUILD)/resolution.json
	$(PYTHON) tools/simulate.py --plan generated/plan.json --resolution $(BUILD)/resolution.json --rom $(BUILD)/game.rom --runner $(BUILD)/neo8-runner --output $(BUILD)/execution.json $(SIMFLAGS)

$(BUILD)/neo8-runner: tools/neo8_runner.cpp vendor/chips/z80.h $(BUILD)/runner-settings.txt | $(BUILD)
	$(CXX) -std=c++17 $(RUNNER_CXXFLAGS) $< -o $@

$(BUILD):
	mkdir -p $@

FORCE:
$(BUILD)/runner-settings.txt: FORCE | $(BUILD)
	@{ $(CXX) --version; printf '%s\n' '$(CXX)' '$(RUNNER_CXXFLAGS)'; } > $(BUILD)/runner-settings.next
	@cmp -s $(BUILD)/runner-settings.next $@ || cp $(BUILD)/runner-settings.next $@

$(BUILD)/toolchain.txt: FORCE | $(BUILD)
	@{ $(SDCC) --version; printf '%s\n' '$(SDCCFLAGS)' '$(CPPFLAGS)' '$(AS)' '$(MEGALINKER)' '$(LIBS)' '$(SELECTED)' '$(USER_SOURCES)'; } > $(BUILD)/toolchain.next
	@cmp -s $(BUILD)/toolchain.next $@ || cp $(BUILD)/toolchain.next $@

$(BUILD)/megalinker: vendor/megalinker/megalinker.cc | $(BUILD)
	$(CXX) -std=c++17 -O2 $< -o $@

$(BUILD)/crt0.rel: runtime/crt0.s $(BUILD)/toolchain.txt | $(BUILD)
	$(AS) -plosgff -o $@ $<
$(BUILD)/platform.rel: runtime/platform.s $(BUILD)/toolchain.txt | $(BUILD)
	$(AS) -plosgff -o $@ $<
$(BUILD)/mapper.rel: runtime/mapper.s $(BUILD)/toolchain.txt | $(BUILD)
	$(AS) -plosgff -o $@ $<
$(BUILD)/vx_mapper.rel: runtime/vx_mapper.c $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) --codeseg HOME -c $< -o $@
$(BUILD)/vx_runtime.rel: runtime/vx_runtime.c $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/vx_fixed.rel: runtime/vx_fixed.c $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/vx_float.rel: runtime/vx_float.c $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/vx_math.rel: runtime/vx_math.c $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/vx_bridges.rel: $(BRIDGE_SOURCE) $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) --constseg HOME -c $< -o $@

define BANK_RULE
$(BUILD)/$(1).rel: $(call source,$(1)) $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(if $(filter %.s,$(call source,$(1))),$(AS) -plosgff -o $$@ $$<,$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) -c $$< -o $$@)
endef
$(foreach m,$(BANK_MODULES),$(eval $(call BANK_RULE,$(m))))

$(BUILD)/user_%.rel: user/%.c $(HEADERS) local.mk Makefile $(BUILD)/toolchain.txt | $(BUILD)
	$(SDCC) $(SDCCFLAGS) $(CPPFLAGS) --codeseg HOME -c $< -o $@
$(BUILD)/user_%.rel: user/%.s $(BUILD)/toolchain.txt | $(BUILD)
	$(AS) -plosgff -o $@ $<

$(BUILD)/game.rom: $(OBJECTS) $(MEGALINKER) $(LIBS) generated/plan.json generated/source-fingerprints.json tools/check_project.py tools/stack_usage.py user/stack-contracts.json
	@$(RM) $(BUILD)/execution.json
	$(MEGALINKER) --segment-size 8 --segment-count $(ROM_SEGMENTS) $@ $(OBJECTS) $(LIBS) > $(BUILD)/link.log
	$(PYTHON) tools/check_project.py generated/plan.json $@ $(BUILD)/resolution.json

clean:
	rm -rf $(BUILD)
