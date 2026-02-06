CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
BUILD_DIR ?= $(abspath ../build)

CXXFLAGS += -Isrc

OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/frontend/%.o,$(wildcard src/*.cpp))
LIB_OBJECTS = $(filter-out $(BUILD_DIR)/frontend/frontend_main.o,$(OBJECTS))
CLI_OBJECT = $(BUILD_DIR)/frontend/frontend_main.o

# Recursive wildcard function
rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

TEST_TARGETS := $(patsubst %/test.vx,$(BUILD_DIR)/frontend-%/test_report.md,$(call rwildcard,tests/,test.vx))
TEST_TARGETS += $(patsubst %/test.sh,$(BUILD_DIR)/frontend-%/test_report.md,$(call rwildcard,tests/,test.sh))
TEST_CHECK_TARGETS := $(patsubst %.md,%.checked,$(TEST_TARGETS))

.PHONY: all clean test update-test-reports debug-invariants-test

all: $(BUILD_DIR)/vexel-frontend $(BUILD_DIR)/lib/libvexelfrontend.a

$(BUILD_DIR)/lib/libvexelfrontend.a: $(LIB_OBJECTS)
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

$(BUILD_DIR)/vexel-frontend: $(CLI_OBJECT) $(BUILD_DIR)/lib/libvexelfrontend.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/frontend/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: $(TEST_CHECK_TARGETS)
	@echo "All tests passed!"

debug-invariants-test:
	+$(MAKE) clean
	+$(MAKE) CXXFLAGS="$(CXXFLAGS) -O0 -g -DVEXEL_DEBUG_PASS_INVARIANTS" test

update-test-reports: $(TEST_TARGETS)
	@echo "Updating test reports..."
	@updated=0; \
	for report in $(TEST_TARGETS); do \
		expected=$$(echo $$report | sed 's|$(BUILD_DIR)/frontend-tests/|tests/|; s|test_report\.md|expected_report.md|'); \
		if [ ! -f $$expected ] || ! diff -q $$report $$expected > /dev/null 2>&1; then \
			mkdir -p $$(dirname $$expected); \
			cp $$report $$expected; \
			updated=$$((updated + 1)); \
		fi; \
	done; \
	echo "Updated $$updated test report(s). Use 'git diff' to review changes."

$(BUILD_DIR)/frontend-tests/%/test_report.md: $(abspath tests)/%/test.vx all
	@mkdir -p $(@D)
	@{ \
		stdout_file=$$(mktemp); \
		stderr_file=$$(mktemp); \
		if $(BUILD_DIR)/vexel-frontend tests/$*/test.vx > $$stdout_file 2> $$stderr_file; then \
			exit_code=0; \
		else \
			exit_code=$$?; \
		fi; \
		echo "## Stdout" > $@; \
		echo '```' >> $@; \
		cat $$stdout_file >> $@; \
		echo '```' >> $@; \
		echo "" >> $@; \
		echo "## Stderr" >> $@; \
		echo '```' >> $@; \
		cat $$stderr_file >> $@; \
		echo '```' >> $@; \
		echo "" >> $@; \
		echo "## Exit Code" >> $@; \
		echo "$$exit_code" >> $@; \
		rm -f $$stdout_file $$stderr_file; \
	}

$(BUILD_DIR)/frontend-tests/%/test_report.md: $(abspath tests)/%/test.sh all
	@mkdir -p $(@D)
	@{ \
		stdout_file=$$(mktemp); \
		stderr_file=$$(mktemp); \
		if cd tests/$* && bash test.sh > $$stdout_file 2> $$stderr_file; then \
			exit_code=0; \
		else \
			exit_code=$$?; \
		fi; \
		echo "## Stdout" > $@; \
		echo '```' >> $@; \
		cat $$stdout_file >> $@; \
		echo '```' >> $@; \
		echo "" >> $@; \
		echo "## Stderr" >> $@; \
		echo '```' >> $@; \
		cat $$stderr_file >> $@; \
		echo '```' >> $@; \
		echo "" >> $@; \
		echo "## Exit Code" >> $@; \
		echo "$$exit_code" >> $@; \
		rm -f $$stdout_file $$stderr_file; \
	}

# Verify test report against expected output
$(BUILD_DIR)/frontend-tests/%/test_report.checked: $(BUILD_DIR)/frontend-tests/%/test_report.md tests/%/expected_report.md
	@if ! diff -q $< tests/$*/expected_report.md > /dev/null 2>&1; then \
		echo "FAIL: tests/$*"; \
		exit 1; \
	fi
	@touch $@

clean:
	rm -f $(OBJECTS)
	rm -f $(OBJECTS:.o=.d)
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/frontend
	rm -f $(BUILD_DIR)/lib/libvexelfrontend.a
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)/lib
	rm -f $(BUILD_DIR)/vexel-frontend
	rm -f $(TEST_TARGETS) $(TEST_CHECK_TARGETS)
	find $(BUILD_DIR)/frontend-tests -type d -empty -delete 2>/dev/null || true
	rmdir --ignore-fail-on-non-empty $(BUILD_DIR)
	

-include $(OBJECTS:.o=.d)
