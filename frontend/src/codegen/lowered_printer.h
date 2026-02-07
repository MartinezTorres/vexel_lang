#pragma once
#include "ast.h"
#include <string>

namespace vexel {

// Pretty-prints the canonical lowered module consumed by backends.
// This output is a debugging/interop surface for Stage-1 results:
// - annotations preserved verbatim as [[...]]
// - compile-time-dead branches already pruned
// - statement-only nodes keep null value type
std::string print_lowered_module(const Module& mod);

}
