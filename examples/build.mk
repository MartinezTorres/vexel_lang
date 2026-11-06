# Run from the repository root: make -f examples/build.mk
VEXEL ?= build/vexel
ARTIFACT := build/palette-plan.json

.PHONY: all
all: $(ARTIFACT)

$(VEXEL):
	$(MAKE)

$(ARTIFACT): examples/build.vx $(VEXEL)
	@mkdir -p build
	$(VEXEL) $< --json --depfile build/palette-plan.d --target $@ > $@.tmp
	mv $@.tmp $@

-include build/palette-plan.d
