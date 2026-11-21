# One supported frontend: the relational solver and its source adapter.
# src/parser.cpp and the shared numeric/resource helpers are reused; the earlier
# Solver implementation and its historical tests are not part of this build.
CXX ?= c++
BUILD ?= build
.DELETE_ON_ERROR:
.PHONY: all relational test test-relational test-incremental incremental-msx sanitize test-msx examples clean relational-msx
all: $(BUILD)/vexel

relational:
	$(MAKE) -C experimental/relational BUILD=$(abspath $(BUILD)/relational)

$(BUILD)/vexel: relational
	@mkdir -p $(BUILD)
	@cmp -s $(BUILD)/relational/vexel-source $@ || { cp $(BUILD)/relational/vexel-source $@.next && mv -f $@.next $@; }

test: all
	$(MAKE) -C experimental/relational BUILD=$(abspath $(BUILD)/relational) test

test-relational: test

.PHONY: test-examples
EXAMPLE_OUTPUT ?= $(BUILD)/program-examples
test-examples: all
	python3 experimental/relational/tests/check_examples.py $(BUILD)/vexel --output "$(EXAMPLE_OUTPUT)" --msx

test-incremental: all
	$(MAKE) -C experimental/relational BUILD=$(abspath $(BUILD)/relational) test-incremental

incremental-msx: all
	python3 examples/incremental_msx.py $(BUILD)/vexel --output $(BUILD)/incremental-msx

.PHONY: test-interface test-closure closure-msx bank-pressure-msx msx-project
test-interface: all
	$(MAKE) -C experimental/relational BUILD=$(abspath $(BUILD)/relational) test-interface

test-closure: all
	$(MAKE) -C experimental/relational BUILD=$(abspath $(BUILD)/relational) test-closure

closure-msx: all
	python3 examples/closure_msx.py $(BUILD)/vexel --output $(BUILD)/closure-msx

bank-pressure-msx: all
	python3 examples/bank_pressure_msx.py $(BUILD)/vexel

msx-project: all
	python3 experimental/relational/realize_msx.py $(SOURCE) $(OUTPUT) --solver $(BUILD)/vexel --build $(VEXEL_OPTIONS)

# Keep sanitizer execution a direct Make command, including log redirection
# inside the recipe so the existing make-sanitize approval covers every run.
sanitize:
	$(MAKE) -C experimental/relational sanitize $(if $(strip $(SANITIZE_LOG)),SANITIZE_LOG="$(abspath $(SANITIZE_LOG))")

test-msx: all
	python3 examples/relational_capabilities_msx.py $(BUILD)/vexel --output $(BUILD)/relational-capabilities-msx

.PHONY: publication-msx
publication-msx: all
	@mkdir -p "$(BUILD)/publication-msx"
	@set -e; publication_dir=$$(mktemp -d "$(BUILD)/publication-msx/run-XXXXXX"); \
	printf 'Publication evidence: %s\n' "$$publication_dir"; \
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx --case word --prefer calculation_publication_narrow=0 --prefer calculation_publication_reuse=0 --output "$$publication_dir/wide-separate"; \
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx --case word --prefer calculation_publication_narrow=0 --prefer calculation_publication_reuse=1 --output "$$publication_dir/wide-shared"; \
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx --case word --prefer calculation_publication_narrow=1 --prefer calculation_publication_reuse=0 --output "$$publication_dir/narrow-separate"; \
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx --case word --prefer calculation_publication_narrow=1 --prefer calculation_publication_reuse=1 --output "$$publication_dir/narrow-shared"

.PHONY: value-msx
value-msx: all
	python3 experimental/relational/tests/test_division_unroll.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_transport_domains.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_ranges.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx --case rotations --prefer calculation_rotation_helpers=1 --prefer value_rotation_helpers=1
	python3 experimental/relational/tests/test_value_widths.py $(BUILD)/vexel --msx --case stored_rotations --prefer calculation_rotation_helpers=1 --prefer value_rotation_helpers=1
	python3 experimental/relational/tests/test_bit_demand.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_constant_division.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_boundary_expansion.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_region_predicates.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_runtime_components.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_products.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_sparse_products.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_finite_cuts.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_table_policy.py $(BUILD)/vexel
	python3 experimental/relational/tests/test_finite_storage.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_finite_context.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_frontiers.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_finite_thresholds.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_finite_native.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_branch_facts.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_literal_operands.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_case_facts.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_case_partitions.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_projection.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_result_demand.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_interface_demand.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_value_facts.py $(BUILD)/vexel --msx
	python3 experimental/relational/tests/test_equalities.py $(BUILD)/vexel --msx

relational-msx: relational-source-msx

relational-source-msx: relational
	python3 examples/relational_source_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-source-msx

relational-control-msx: relational
	python3 examples/relational_control_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-control-msx

relational-execution-msx: relational
	python3 examples/relational_execution_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-execution-msx

relational-completion-msx: relational
	python3 examples/relational_completion_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-completion-msx

relational-boundary-msx: relational
	python3 examples/relational_boundary_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-boundary-msx

relational-types-msx: relational
	python3 examples/relational_types_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-types-msx

relational-schema-msx: relational
	python3 examples/relational_schema_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-schema-msx

.PHONY: relational-operators-msx
relational-operators-msx: relational
	python3 examples/relational_operators_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-operators-msx

.PHONY: relational-bindings-msx
relational-bindings-msx: relational
	python3 examples/relational_bindings_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-bindings-msx

.PHONY: relational-strings-msx
relational-strings-msx: relational
	python3 examples/relational_strings_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-strings-msx

.PHONY: relational-signed-msx
relational-signed-msx: relational
	python3 examples/relational_signed_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-signed-msx

.PHONY: relational-numeric-msx
relational-numeric-msx: relational
	python3 examples/relational_numeric_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-numeric-msx

.PHONY: relational-representation-msx
relational-representation-msx: relational
	python3 examples/relational_representation_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-representation-msx

.PHONY: relational-tuples-msx
relational-tuples-msx: relational
	python3 examples/relational_tuples_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-tuples-msx

.PHONY: relational-methods-msx
relational-methods-msx: relational
	python3 examples/relational_methods_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-methods-msx

.PHONY: relational-broadcast-msx
relational-broadcast-msx: relational
	python3 examples/relational_broadcast_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-broadcast-msx

.PHONY: relational-iteration-msx
relational-iteration-msx: relational
	python3 examples/relational_iteration_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-iteration-msx

.PHONY: relational-sorted-msx
relational-sorted-msx: relational
	python3 examples/relational_sorted_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-sorted-msx

.PHONY: relational-resources-msx
relational-resources-msx: relational
	python3 examples/relational_resources_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-resources-msx

.PHONY: relational-modules-msx
relational-modules-msx: relational
	python3 examples/relational_modules_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-modules-msx

.PHONY: relational-math-msx
relational-math-msx: relational
	python3 examples/relational_math_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-math-msx

.PHONY: relational-banks-msx
relational-banks-msx: relational
	python3 examples/relational_banks_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-banks-msx

.PHONY: relational-specialization-msx
relational-specialization-msx: relational
	python3 examples/relational_specialization_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-specialization-msx

.PHONY: relational-frame-facts-msx
relational-frame-facts-msx: relational
	python3 examples/relational_frame_facts_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-frame-facts-msx

.PHONY: relational-inputs-msx
relational-inputs-msx: relational
	python3 examples/relational_inputs_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-inputs-msx

.PHONY: relational-frame-plan-msx
relational-frame-plan-msx: relational
	python3 examples/relational_frame_plan_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-frame-plan-msx

.PHONY: relational-contracts-msx
relational-contracts-msx: relational
	python3 examples/relational_contracts_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-contracts-msx

.PHONY: relational-occurrences-msx
relational-occurrences-msx: relational
	python3 examples/relational_occurrences_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-occurrences-msx

.PHONY: relational-loop-init-msx
relational-loop-init-msx: relational
	python3 examples/relational_loop_initialization_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-loop-init-msx

.PHONY: relational-function-types-msx
relational-function-types-msx: relational
	python3 examples/relational_function_types_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-function-types-msx

.PHONY: relational-capture-closure-msx
relational-capture-closure-msx: relational
	python3 examples/relational_capture_closure_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-capture-closure-msx

.PHONY: relational-capture-modes-msx
relational-capture-modes-msx: relational
	python3 examples/relational_capture_modes_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-capture-modes-msx

.PHONY: relational-recursive-captures-msx
relational-recursive-captures-msx: relational
	python3 examples/relational_recursive_captures_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-recursive-captures-msx

.PHONY: relational-capabilities-msx
relational-capabilities-msx: relational
	python3 examples/relational_capabilities_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-capabilities-msx

relational-sudoku-oracle: relational
	python3 experimental/relational/tests/oracle_sudoku.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-sudoku-oracle.json

relational-aggregate-msx: relational
	python3 examples/relational_aggregate_msx.py $(BUILD)/relational/vexel-source --output $(BUILD)/relational-aggregate-msx


examples: all
	$(BUILD)/vexel examples/puzzle.vx
	$(BUILD)/vexel examples/sudoku.vx --query value:solution --max-work 20000000

clean:
	rm -rf $(BUILD)

.PHONY: calculation-msx
calculation-msx: all
	python3 experimental/relational/tests/test_calculation_scheduling.py $(BUILD)/vexel --msx
