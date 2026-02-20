#include "cli_utils.h"
#include "common.h"

#include <algorithm>
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

bool parse_type_strictness_value(const char* value, int& out_level) {
    if (!value || *value == '\0') return false;

    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "relaxed") == 0) {
        out_level = 0;
        return true;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "annotated-locals") == 0) {
        out_level = 1;
        return true;
    }
    if (std::strcmp(value, "2") == 0 || std::strcmp(value, "full") == 0) {
        out_level = 2;
        return true;
    }
    return false;
}

bool parse_type_strictness_arg(const char* value, Compiler::Options& opts, std::string& error) {
    int parsed = 0;
    if (!parse_type_strictness_value(value, parsed)) {
        error = "--type-strictness expects one of: 0,1,2 (or relaxed,annotated-locals,full)";
        return false;
    }
    opts.type_strictness = parsed;
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
    if (std::strcmp(argv[index], "--strict-types") == 0) {
        opts.type_strictness = std::max(opts.type_strictness, 1);
        return true;
    }
    constexpr const char* kStrictTypesPrefix = "--strict-types=";
    if (std::strncmp(argv[index], kStrictTypesPrefix, std::strlen(kStrictTypesPrefix)) == 0) {
        const char* value = argv[index] + std::strlen(kStrictTypesPrefix);
        if (std::strcmp(value, "full") == 0) {
            opts.type_strictness = std::max(opts.type_strictness, 2);
            return true;
        }
        int parsed = 0;
        if (!parse_type_strictness_value(value, parsed)) {
            error = "--strict-types expects no value, '=full', or '=2'";
            return true;
        }
        opts.type_strictness = std::max(opts.type_strictness, parsed);
        return true;
    }
    if (std::strcmp(argv[index], "--type-strictness") == 0) {
        if (index + 1 >= argc) {
            error = "--type-strictness requires an argument";
            return true;
        }
        const char* value = argv[++index];
        (void)parse_type_strictness_arg(value, opts, error);
        return true;
    }
    constexpr const char* kTypeStrictnessPrefix = "--type-strictness=";
    if (std::strncmp(argv[index], kTypeStrictnessPrefix, std::strlen(kTypeStrictnessPrefix)) == 0) {
        const char* value = argv[index] + std::strlen(kTypeStrictnessPrefix);
        (void)parse_type_strictness_arg(value, opts, error);
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
