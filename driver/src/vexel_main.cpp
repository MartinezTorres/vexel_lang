#include "compiler.h"
#include <iostream>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <vector>

#ifndef BACKENDS
#define BACKENDS "c,banked"
#endif

static std::vector<std::string> parse_backends(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t comma = s.find(',', start);
        std::string name = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!name.empty()) out.push_back(name);
        if (comma == std::string::npos) break;
        start = comma + 1;
        while (start < s.size() && s[start] == ' ') start++;
    }
    // Prefer c as default when available for compatibility with existing tests/tools.
    auto it = std::find(out.begin(), out.end(), std::string("c"));
    if (it != out.end() && it != out.begin()) {
        std::rotate(out.begin(), it, it + 1);
    }
    return out;
}

// Unified CLI: select backend by -b/--backend (auto-discovered). Defaults to first available (prefers c).
static void print_usage(const char* prog, const std::vector<std::string>& backends) {
    std::cout << "Vexel Compiler (multi-backend)\n";
    std::cout << "Usage: " << prog << " [options] <input.vx>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o <path>    Output path (base name for generated files, default: out)\n";
    std::cout << "  -b <name>    Backend: ";
    for (size_t i = 0; i < backends.size(); ++i) {
        std::cout << backends[i];
        if (i + 1 < backends.size()) std::cout << ", ";
    }
    std::cout << "\n";
    std::cout << "  -L           Emit lowered Vexel subset alongside backend output\n";
    std::cout << "  --allow-process Enable process expressions (executes host commands; disabled by default)\n";
    std::cout << "  -v           Verbose output\n";
    std::cout << "  -h           Show this help\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> available_backends = parse_backends(BACKENDS);
    if (available_backends.empty()) {
        std::cerr << "No backends available\n";
        return 1;
    }

    vexel::Compiler::Options opts;
    opts.output_file = "out";
    opts.backend = available_backends[0] == "banked"
        ? vexel::Compiler::Options::BackendKind::Banked
        : vexel::Compiler::Options::BackendKind::C;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0], available_backends);
            return 0;
        } else if (std::strcmp(argv[i], "-v") == 0) {
            opts.verbose = true;
        } else if (std::strcmp(argv[i], "-L") == 0 || std::strcmp(argv[i], "--emit-lowered") == 0) {
            opts.emit_lowered = true;
        } else if (std::strcmp(argv[i], "--allow-process") == 0) {
            opts.allow_process = true;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                std::cerr << "Error: -o requires an argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "-b") == 0 || std::strcmp(argv[i], "--backend") == 0) {
            if (i + 1 < argc) {
                const char* backend = argv[++i];
                if (std::strcmp(backend, "c") == 0) {
                    opts.backend = vexel::Compiler::Options::BackendKind::C;
                } else if (std::strcmp(backend, "banked") == 0) {
                    opts.backend = vexel::Compiler::Options::BackendKind::Banked;
                } else {
                    std::cerr << "Error: Unknown backend '" << backend << "'\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: -b/--backend requires an argument\n";
                return 1;
            }
        } else if (argv[i][0] == '-') {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            return 1;
        } else {
            opts.input_file = argv[i];
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0], available_backends);
        return 1;
    }

    try {
        vexel::Compiler compiler(opts);
        (void)compiler.compile();
        return 0;
    } catch (const vexel::CompileError& e) {
        std::cerr << "Error";
        if (!e.location.filename.empty()) {
            std::cerr << " at " << e.location.filename
                     << ":" << e.location.line
                     << ":" << e.location.column;
        }
        std::cerr << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
