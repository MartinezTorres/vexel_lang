#pragma once

#include "compiler.h"

#include <ostream>
#include <string>

namespace vexel {

bool try_read_backend_arg(int argc,
                          char** argv,
                          int& index,
                          std::string& out_backend,
                          std::string& error);

bool try_parse_common_compiler_option(int argc,
                                      char** argv,
                                      int& index,
                                      Compiler::Options& opts,
                                      std::string& error);

bool try_parse_backend_opt_arg(int argc,
                               char** argv,
                               int& index,
                               Compiler::Options& opts,
                               std::string& error);

int run_compiler_with_diagnostics(const Compiler::Options& opts, std::ostream& err);

} // namespace vexel
