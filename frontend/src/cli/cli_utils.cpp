#include "cli_utils.h"

#include <cstring>

namespace vexel {

namespace {

bool parse_backend_opt_value(const char* opt, Compiler::Options& opts, std::string& error) {
    const char* eq = std::strchr(opt, '=');
    if (!eq || eq == opt || *(eq + 1) == '\0') {
        error = "--backend-opt expects key=value";
        return false;
    }
    std::string key(opt, eq - opt);
    std::string value(eq + 1);
    opts.backend_options[key] = value;
    return true;
}

} // namespace

bool try_read_backend_arg(int argc,
                          char** argv,
                          int& index,
                          std::string& out_backend,
                          std::string& error) {
    if (std::strcmp(argv[index], "-b") == 0 || std::strcmp(argv[index], "--backend") == 0) {
        if (index + 1 >= argc) {
            error = "-b/--backend requires an argument";
            return true;
        }
        out_backend = argv[++index];
        return true;
    }

    constexpr const char* kPrefix = "--backend=";
    if (std::strncmp(argv[index], kPrefix, std::strlen(kPrefix)) == 0) {
        const char* value = argv[index] + std::strlen(kPrefix);
        if (*value == '\0') {
            error = "--backend requires a non-empty value";
            return true;
        }
        out_backend = value;
        return true;
    }
    return false;
}

bool try_parse_common_compiler_option(int argc,
                                      char** argv,
                                      int& index,
                                      Compiler::Options& opts,
                                      std::string& error) {
    if (std::strcmp(argv[index], "-v") == 0) {
        opts.verbose = true;
        return true;
    }
    if (std::strcmp(argv[index], "--emit-analysis") == 0) {
        opts.emit_analysis = true;
        return true;
    }
    if (std::strcmp(argv[index], "--allow-process") == 0) {
        opts.allow_process = true;
        return true;
    }
    if (std::strcmp(argv[index], "-o") == 0) {
        if (index + 1 >= argc) {
            error = "-o requires an argument";
            return true;
        }
        opts.output_file = argv[++index];
        return true;
    }
    return false;
}

bool try_parse_backend_opt_arg(int argc,
                               char** argv,
                               int& index,
                               Compiler::Options& opts,
                               std::string& error) {
    if (std::strcmp(argv[index], "--backend-opt") != 0 &&
        std::strncmp(argv[index], "--backend-opt=", 14) != 0) {
        return false;
    }

    const char* opt = nullptr;
    if (std::strncmp(argv[index], "--backend-opt=", 14) == 0) {
        opt = argv[index] + 14;
    } else if (index + 1 < argc) {
        opt = argv[++index];
    } else {
        error = "--backend-opt requires an argument";
        return true;
    }

    (void)parse_backend_opt_value(opt, opts, error);
    return true;
}

int run_compiler_with_diagnostics(const Compiler::Options& opts, std::ostream& err) {
    try {
        Compiler compiler(opts);
        (void)compiler.compile();
        return 0;
    } catch (const CompileError& e) {
        err << "Error";
        if (!e.location.filename.empty()) {
            err << " at " << e.location.filename
                << ":" << e.location.line
                << ":" << e.location.column;
        }
        err << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        err << "Error: " << e.what() << "\n";
        return 1;
    }
}

} // namespace vexel
