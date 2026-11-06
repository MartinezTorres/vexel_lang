CXX ?= c++
BUILD ?= ../../build/relational
# Dependency files must name the same targets whether make starts here or at
# the repository root. Mixing relative and absolute BUILD hid header changes.
override BUILD := $(abspath $(BUILD))
CPPFLAGS += -Iinclude -I../../include -DVEXEL_LIBRARY_DIR='"$(abspath ../../lib)"'
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
OBJECTS := $(BUILD)/machine.o $(BUILD)/adapters.o
SOURCE_OBJECTS := $(BUILD)/source.o $(BUILD)/source_rules.o $(BUILD)/source_signed.o $(BUILD)/source_numeric.o $(BUILD)/syntax_parser.o $(BUILD)/syntax_graph.o $(BUILD)/syntax_floating.o $(BUILD)/syntax_process.o
SOURCE_OBJECTS += $(BUILD)/syntax_math_numeric.o
SOURCE_OBJECTS += $(BUILD)/source_observation.o $(BUILD)/source_control_proof.o
SOURCE_OBJECTS += $(BUILD)/source_workspace.o
SOURCE_OBJECTS += $(BUILD)/source_bank_chunks.o
.DELETE_ON_ERROR:
.PHONY: all test test-closure test-incremental sanitize
SANITIZE_TARGET ?= test
all: $(BUILD)/vexel-relational $(BUILD)/vexel-source

$(BUILD)/%.o: src/%.cpp include/machine.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/vexel-relational: $(OBJECTS) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

# Reuse only source recognition and its arena; no old Solver object is linked.
$(BUILD)/syntax_%.o: ../../src/%.cpp ../../include/vexel.hpp ../../include/floating.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/vexel-source: $(OBJECTS) $(SOURCE_OBJECTS) $(BUILD)/source_main.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD)/test-machine: tests/test_machine.cpp $(OBJECTS) include/machine.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) -o $@

$(BUILD)/test-control-proof: tests/test_control_proof.cpp $(OBJECTS) $(BUILD)/source_control_proof.o include/source_control_proof.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) $(BUILD)/source_control_proof.o -o $@

test: $(BUILD)/vexel-relational $(BUILD)/vexel-source $(BUILD)/test-machine $(BUILD)/test-control-proof
	$(BUILD)/test-machine
	$(BUILD)/test-control-proof
	python3 tests/test_joint.py $(BUILD)/vexel-relational
	python3 tests/test_source.py $(BUILD)/vexel-source
	python3 tests/test_execution.py $(BUILD)/vexel-source
	python3 tests/test_aggregates.py $(BUILD)/vexel-source
	python3 tests/test_completion.py $(BUILD)/vexel-source
	python3 tests/test_loop_initialization.py $(BUILD)/vexel-source
	python3 tests/test_numeric_loop.py $(BUILD)/vexel-source
	python3 tests/test_runtime_readiness.py $(BUILD)/vexel-source
	python3 tests/test_numeric_control.py $(BUILD)/vexel-source
	python3 tests/test_defined_arithmetic.py $(BUILD)/vexel-source
	python3 tests/test_native_control.py $(BUILD)/vexel-source
	python3 tests/test_native_capture_control.py $(BUILD)/vexel-source
	python3 tests/test_native_receiver_control.py $(BUILD)/vexel-source
	python3 tests/test_finite.py $(BUILD)/vexel-source
	python3 tests/test_observations.py $(BUILD)/vexel-source
	python3 tests/test_expressions.py $(BUILD)/vexel-source
	python3 tests/test_boundary.py $(BUILD)/vexel-source
	python3 tests/test_type_closure.py $(BUILD)/vexel-source
	python3 tests/test_function_types.py $(BUILD)/vexel-source
	python3 tests/test_schema_invariants.py $(BUILD)/vexel-source
	python3 tests/test_operators.py $(BUILD)/vexel-source
	python3 tests/test_strings.py $(BUILD)/vexel-source
	python3 tests/test_signed.py $(BUILD)/vexel-source
	python3 tests/test_numeric.py $(BUILD)/vexel-source
	python3 tests/test_aggregate_operators.py $(BUILD)/vexel-source
	python3 tests/test_tuples.py $(BUILD)/vexel-source
	python3 tests/test_methods.py $(BUILD)/vexel-source
	python3 tests/test_probes.py $(BUILD)/vexel-source
	python3 tests/test_capabilities.py $(BUILD)/vexel-source
	python3 tests/test_operator_methods.py $(BUILD)/vexel-source
	python3 tests/test_broadcast.py $(BUILD)/vexel-source
	python3 tests/test_iteration_methods.py $(BUILD)/vexel-source
	python3 tests/test_sorted_iteration.py $(BUILD)/vexel-source
	python3 tests/test_resources.py $(BUILD)/vexel-source
	python3 tests/test_modules.py $(BUILD)/vexel-source
	python3 tests/test_incremental.py $(BUILD)/vexel-source
	python3 tests/test_incremental_paths.py $(BUILD)/vexel-source
	python3 tests/test_math.py $(BUILD)/vexel-source
	python3 tests/test_banks.py $(BUILD)/vexel-source
	python3 tests/test_shapes.py $(BUILD)/vexel-source
	python3 tests/test_specialization.py $(BUILD)/vexel-source
	python3 tests/test_frame_facts.py $(BUILD)/vexel-source
	python3 tests/test_capture_closure.py $(BUILD)/vexel-source
	python3 tests/test_recursive_captures.py $(BUILD)/vexel-source
	python3 tests/test_capture_modes.py $(BUILD)/vexel-source
	python3 tests/test_output_encodings.py $(BUILD)/vexel-source
	python3 tests/test_inputs.py $(BUILD)/vexel-source
	python3 tests/test_frame_plan.py $(BUILD)/vexel-source
	python3 tests/test_contracts.py $(BUILD)/vexel-source
	python3 tests/test_occurrences.py $(BUILD)/vexel-source
	python3 tests/test_occurrence_frames.py $(BUILD)/vexel-source
	python3 tests/test_initializer_frames.py $(BUILD)/vexel-source
	python3 tests/test_boundary_names.py $(BUILD)/vexel-source
	python3 tests/test_msx_layout.py
	python3 tests/test_configuration_streams.py $(BUILD)/vexel-source
	python3 tests/test_backend_closure.py $(BUILD)/vexel-source

test-closure: $(BUILD)/vexel-source
	python3 tests/test_configuration_streams.py $(BUILD)/vexel-source
	python3 tests/test_backend_closure.py $(BUILD)/vexel-source

test-incremental: $(BUILD)/vexel-source
	python3 tests/test_incremental.py $(BUILD)/vexel-source
	python3 tests/test_incremental_paths.py $(BUILD)/vexel-source
	python3 tests/test_resources.py $(BUILD)/vexel-source
	python3 tests/test_modules.py $(BUILD)/vexel-source

sanitize:
	$(MAKE) BUILD=../../build/relational-sanitize CXXFLAGS='-std=c++20 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' $(SANITIZE_TARGET) $(if $(strip $(SANITIZE_LOG)),> "$(SANITIZE_LOG)" 2>&1)

-include $(OBJECTS:.o=.d) $(SOURCE_OBJECTS:.o=.d) $(BUILD)/main.d $(BUILD)/source_main.d
