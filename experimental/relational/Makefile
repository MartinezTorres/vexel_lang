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
SOURCE_OBJECTS += $(BUILD)/source_value.o
SOURCE_OBJECTS += $(BUILD)/source_demand.o
SOURCE_OBJECTS += $(BUILD)/source_execution.o
SOURCE_OBJECTS += $(BUILD)/source_facts.o
SOURCE_OBJECTS += $(BUILD)/source_ranges.o
SOURCE_OBJECTS += $(BUILD)/source_transport.o
SOURCE_OBJECTS += $(BUILD)/source_equalities.o
SOURCE_OBJECTS += $(BUILD)/source_image.o
SOURCE_OBJECTS += $(BUILD)/term_index.o
SOURCE_OBJECTS += $(BUILD)/source_conditional.o $(BUILD)/source_algebra.o
.DELETE_ON_ERROR:
.PHONY: all test test-interface test-closure test-incremental sanitize
SANITIZE_TARGET ?= test
all: $(BUILD)/vexel-relational $(BUILD)/vexel-source

# SourceOptions crosses these translation-unit boundaries. Keep this ABI
# dependency explicit too: older cached .d files used relative BUILD targets
# and cannot invalidate the now-absolute targets until their first rebuild.
$(BUILD)/source.o $(BUILD)/source_workspace.o $(BUILD)/source_image.o $(BUILD)/source_main.o: include/source.hpp

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

$(BUILD)/test-control-proof: tests/test_control_proof.cpp $(OBJECTS) $(BUILD)/source_control_proof.o $(BUILD)/source_algebra.o $(BUILD)/term_index.o include/source_control_proof.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) $(BUILD)/source_control_proof.o $(BUILD)/source_algebra.o $(BUILD)/term_index.o -o $@

$(BUILD)/test-term-index: tests/test_term_index.cpp $(OBJECTS) $(SOURCE_OBJECTS) include/term_index.hpp include/source_demand.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) $(SOURCE_OBJECTS) -o $@

$(BUILD)/test-conditional-proofs: tests/test_conditional_proofs.cpp $(OBJECTS) $(BUILD)/source_control_proof.o $(BUILD)/source_algebra.o $(BUILD)/source_conditional.o $(BUILD)/term_index.o include/source_conditional.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) $(BUILD)/source_control_proof.o $(BUILD)/source_algebra.o $(BUILD)/source_conditional.o $(BUILD)/term_index.o -o $@

$(BUILD)/test-algebra: tests/test_algebra.cpp $(OBJECTS) $(BUILD)/source_control_proof.o $(BUILD)/source_algebra.o $(BUILD)/term_index.o include/source_algebra.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) $(BUILD)/source_control_proof.o $(BUILD)/source_algebra.o $(BUILD)/term_index.o -o $@

$(BUILD)/test-numeric-ranges: tests/test_numeric_ranges.cpp $(OBJECTS) $(BUILD)/source_numeric.o $(BUILD)/syntax_floating.o $(BUILD)/syntax_math_numeric.o include/source_numeric.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< $(OBJECTS) $(BUILD)/source_numeric.o $(BUILD)/syntax_floating.o $(BUILD)/syntax_math_numeric.o -o $@

test-interface: $(BUILD)/vexel-source
	python3 tests/test_shared_proofs.py $(BUILD)/vexel-source
	python3 tests/test_guarded_inputs.py $(BUILD)/vexel-source
	python3 tests/test_conditional_facts.py $(BUILD)/vexel-source
	python3 tests/test_call_contexts.py $(BUILD)/vexel-source
	python3 tests/test_transport.py $(BUILD)/vexel-source
	python3 tests/test_transport_domains.py $(BUILD)/vexel-source
	python3 tests/test_bit_demand.py $(BUILD)/vexel-source
	python3 tests/test_ranges.py $(BUILD)/vexel-source
	python3 tests/test_result_demand.py $(BUILD)/vexel-source
	python3 tests/test_interface_demand.py $(BUILD)/vexel-source
	python3 tests/test_value_facts.py $(BUILD)/vexel-source
	python3 tests/test_equalities.py $(BUILD)/vexel-source
	python3 tests/test_program_image.py $(BUILD)/vexel-source

test: $(BUILD)/vexel-relational $(BUILD)/vexel-source $(BUILD)/test-machine $(BUILD)/test-control-proof $(BUILD)/test-term-index $(BUILD)/test-conditional-proofs $(BUILD)/test-algebra $(BUILD)/test-numeric-ranges
	python3 tests/test_shared_proofs.py $(BUILD)/vexel-source
	python3 tests/test_guarded_inputs.py $(BUILD)/vexel-source
	python3 tests/test_conditional_facts.py $(BUILD)/vexel-source
	python3 tests/test_call_contexts.py $(BUILD)/vexel-source
	python3 tests/test_transport.py $(BUILD)/vexel-source
	python3 tests/test_transport_domains.py $(BUILD)/vexel-source
	python3 tests/test_bit_demand.py $(BUILD)/vexel-source
	$(BUILD)/test-numeric-ranges
	python3 tests/test_constant_division.py $(BUILD)/vexel-source
	python3 tests/test_constant_division.py $(BUILD)/vexel-source --sharing
	python3 tests/test_division_policy.py $(BUILD)/vexel-source
	python3 tests/test_rotation_policy.py $(BUILD)/vexel-source
	python3 tests/test_division_unroll.py $(BUILD)/vexel-source
	python3 tests/test_constant_division.py $(BUILD)/vexel-source --prefer calculation_division_unroll=0 --prefer value_division_unroll=0
	python3 tests/test_ranges.py $(BUILD)/vexel-source
	python3 tests/test_sdcc_guard.py
	python3 tests/test_measured_search.py
	python3 tests/test_result_demand.py $(BUILD)/vexel-source
	python3 tests/test_interface_demand.py $(BUILD)/vexel-source
	python3 tests/test_value_facts.py $(BUILD)/vexel-source
	python3 tests/test_equalities.py $(BUILD)/vexel-source
	python3 tests/test_program_image.py $(BUILD)/vexel-source
	python3 tests/test_region_projection.py $(BUILD)/vexel-source
	python3 tests/test_region_predicates.py $(BUILD)/vexel-source
	python3 tests/test_runtime_components.py $(BUILD)/vexel-source
	python3 tests/test_value_products.py $(BUILD)/vexel-source
	python3 tests/test_sparse_products.py $(BUILD)/vexel-source
	python3 tests/test_finite_cuts.py $(BUILD)/vexel-source
	python3 tests/test_value_table_policy.py $(BUILD)/vexel-source
	python3 tests/test_finite_storage.py $(BUILD)/vexel-source
	python3 tests/test_finite_context.py $(BUILD)/vexel-source
	python3 tests/test_value_frontiers.py $(BUILD)/vexel-source
	python3 tests/test_finite_thresholds.py $(BUILD)/vexel-source
	python3 tests/test_finite_native.py $(BUILD)/vexel-source
	python3 tests/test_branch_facts.py $(BUILD)/vexel-source
	python3 tests/test_literal_operands.py $(BUILD)/vexel-source
	python3 tests/test_case_facts.py $(BUILD)/vexel-source
	python3 tests/test_case_partitions.py $(BUILD)/vexel-source
	python3 tests/test_boundary_expansion.py $(BUILD)/vexel-source
	python3 tests/test_calculation_scheduling.py $(BUILD)/vexel-source
	python3 tests/test_value_ranges.py $(BUILD)/vexel-source
	python3 tests/test_value_widths.py $(BUILD)/vexel-source
	python3 ../../tools/test_publish_repositories.py
	python3 tests/test_value_projection.py $(BUILD)/vexel-source
	$(BUILD)/test-machine
	$(BUILD)/test-control-proof
	$(BUILD)/test-term-index
	$(BUILD)/test-conditional-proofs
	$(BUILD)/test-algebra
	python3 tests/test_modular_control.py $(BUILD)/vexel-source
	python3 tests/test_modular_transport.py $(BUILD)/vexel-source
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
	python3 tests/test_publication_widths.py $(BUILD)/vexel-source
	python3 tests/test_publication_policy.py $(BUILD)/vexel-source
	python3 tests/test_publication_carriers.py $(BUILD)/vexel-source
	python3 tests/test_contracts.py $(BUILD)/vexel-source
	python3 tests/test_occurrences.py $(BUILD)/vexel-source
	python3 tests/test_occurrence_frames.py $(BUILD)/vexel-source
	python3 tests/test_initializer_frames.py $(BUILD)/vexel-source
	python3 tests/test_boundary_names.py $(BUILD)/vexel-source
	python3 tests/test_msx_layout.py
	python3 tests/test_configuration_streams.py $(BUILD)/vexel-source
	python3 tests/test_backend_closure.py $(BUILD)/vexel-source
	python3 tests/test_bank_feedback.py $(BUILD)/vexel-source

test-closure: $(BUILD)/vexel-source
	python3 tests/test_configuration_streams.py $(BUILD)/vexel-source
	python3 tests/test_backend_closure.py $(BUILD)/vexel-source
	python3 tests/test_bank_feedback.py $(BUILD)/vexel-source

test-incremental: $(BUILD)/vexel-source
	python3 tests/test_incremental.py $(BUILD)/vexel-source
	python3 tests/test_incremental_paths.py $(BUILD)/vexel-source
	python3 tests/test_resources.py $(BUILD)/vexel-source
	python3 tests/test_modules.py $(BUILD)/vexel-source

sanitize:
	$(MAKE) BUILD=../../build/relational-sanitize CXXFLAGS='-std=c++20 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' $(SANITIZE_TARGET) $(if $(strip $(SANITIZE_LOG)),> "$(SANITIZE_LOG)" 2>&1)

-include $(OBJECTS:.o=.d) $(SOURCE_OBJECTS:.o=.d) $(BUILD)/main.d $(BUILD)/source_main.d
