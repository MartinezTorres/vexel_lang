#pragma once

#include "ast.h"
#include "program.h"

namespace vexel {

void validate_module_invariants(const Module& mod, const char* stage);
void validate_program_invariants(const Program& program, const char* stage);

} // namespace vexel

