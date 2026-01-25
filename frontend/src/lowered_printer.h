#pragma once
#include "ast.h"
#include <string>

namespace vexel {

// Pretty-prints a type-checked, monomorphized module into the lowered Vexel subset.
// Annotations are preserved and emitted as [[...]] prefixes.
std::string print_lowered_module(const Module& mod);

}
