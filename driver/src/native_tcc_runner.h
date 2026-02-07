#pragma once

#include "compiler.h"

#include <ostream>

namespace vexel {

enum class NativeTccMode {
    Run,
    EmitExe,
};

bool native_tcc_supported();
int run_native_with_tcc(const Compiler::Options& opts, NativeTccMode mode, std::ostream& err);

} // namespace vexel

