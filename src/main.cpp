#include "vexel.hpp"
#include <fstream>
#include <iostream>

namespace {
void usage() {
    std::cout <<
        "usage: vexel FILE [--root NAME | --call NAME --arg INTEGER ...] [options]\n"
        "  Default: solve exported globals and functions (with runtime parameters).\n"
        "  --root NAME           Demand a global; may be repeated\n"
        "  --call NAME           Specialize a function with concrete arguments\n"
        "  --arg INTEGER         Supply a decimal integer to --call\n"
        "  --json                Print the resolved facts and residual graph as JSON\n"
        "  --graph FILE          Write a Graphviz graph of demanded obligations\n"
        "  --search-report FILE  Write search results, including failed searches\n"
        "  --depfile FILE        Write dependencies actually read, in Make syntax\n"
        "  --target NAME         Target of the dependency rule (default: resolution)\n"
        "  --emit-msx DIR        Export an editable NEO-8 SDCC + megalinker project\n"
        "  --msx-entry NAME      Zero-argument export called by the sample main\n"
        "  --msx-stack N         Reserve N bytes below 0xF380 (default 1024)\n"
        "  --msx-rom-segments N  Physical cartridge limit, 16..16384 (default 256)\n"
        "  --msx-exclusive NAME  Assert a pure export has no overlapping activations;\n"
        "                        use static temporaries for that function\n"
        "  --backend-assets DIR  Backend templates (default: build-time path)\n"
        "  --trace               Trace fact strengthening to stderr\n"
        "  --schedule MODE       fifo (default), lifo, or random\n"
        "  --strictness N        Literal/declaration diagnostics: 0 (default), 1, 2\n"
        "  --allow-processes     Allow explicitly written shell process imports\n"
        "  --process-timeout N   Process import timeout in milliseconds (default 1000)\n"
        "  --max-nodes N         Graph budget (default 200000)\n"
        "  --max-steps N         Worklist budget (default 2000000)\n"
        "  --max-elements N      Array/unrolled-iteration budget (default 16384)\n"
        "  --max-bits N          Integer/width bit budget (default 65536)\n"
        "  --max-decisions N     Finite search decision budget (default 100000)\n"
        "  --max-checks N        Finite relation check budget (default 100000000)\n"
        "  --branch-order MODE   forward (default), reverse, or random\n"
        "  --heuristic PROGRAM   Optional executable: JSON stdin/stdout branch advice\n"
        "  --heuristic-replay F  Replay advice from a saved search report, offline\n"
        "  --max-proposals N     Advice request budget across revisions (32 per puzzle)\n"
        "  --heuristic-timeout N Provider timeout in milliseconds (default 1000)\n"
        "  --compiled-search D   Build native relation filters in fresh subdirectories\n"
        "  --verify-search-kernel Compare each native filter call with ordinary propagation\n"
        "  --help                Show this help\n";
}
std::size_t positive(const std::string& value) {
    std::size_t n = 0;
    if (value.empty()) throw vx::Error({}, "budget must be a positive integer");
    for (char c : value) {
        if (c < '0' || c > '9' || n > (std::numeric_limits<std::size_t>::max() - static_cast<unsigned>(c-'0')) / 10)
            throw vx::Error({}, "invalid budget: " + value);
        n = n * 10 + static_cast<unsigned>(c-'0');
    }
    if (!n) throw vx::Error({}, "budget must be positive");
    return n;
}
void write(const std::string& file, const std::string& bytes) {
    std::ofstream output(file, std::ios::binary);
    if (!output || !(output << bytes) || !output.flush()) throw vx::Error({}, "cannot write output: " + file);
}
}
int main(int argc, char** argv) {
    vx::Options options;
    std::string input, call, graphFile, depFile, searchFile, target = "resolution";
    std::vector<std::string> roots, args;
    bool json = false;
    vx::MsxOptions msx;
    bool msxOption = false;
    vx::Graph graph;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 == argc) throw vx::Error({}, "missing value for " + arg);
                return argv[++i];
            };
            if (arg == "--help" || arg == "-h") { usage(); return 0; }
            if (arg == "--json") json = true;
            else if (arg == "--trace") options.trace = true;
            else if (arg == "--root") roots.push_back(value());
            else if (arg == "--call") {
                if (!call.empty()) throw vx::Error({}, "--call may only be specified once");
                call = value();
            }
            else if (arg == "--arg") args.push_back(value());
            else if (arg == "--graph") graphFile = value();
            else if (arg == "--search-report") searchFile = value();
            else if (arg == "--depfile") depFile = value();
            else if (arg == "--target") target = value();
            else if (arg == "--emit-msx") msx.directory = value();
            else if (arg == "--msx-entry") { msx.entry = value(); msxOption = true; }
            else if (arg == "--msx-exclusive") { msx.exclusiveEntries.insert(value()); msxOption = true; }
            else if (arg == "--backend-assets") { msx.assets = value(); msxOption = true; }
            else if (arg == "--msx-stack" || arg == "--msx-rom-segments") {
                const auto n = positive(value());
                if (n > 65535) throw vx::Error({}, "MSX option exceeds 65535");
                if (arg == "--msx-stack") msx.stackBytes = static_cast<unsigned>(n);
                else msx.romSegments = static_cast<unsigned>(n);
                msxOption = true;
            }
            else if (arg == "--schedule") options.schedule = value();
            else if (arg == "--strictness") {
                std::string level = value();
                if (level != "0" && level != "1" && level != "2") throw vx::Error({},"strictness must be 0, 1, or 2");
                options.strictness = static_cast<unsigned>(level[0]-'0');
            }
            else if (arg == "--allow-processes") options.allowProcesses = true;
            else if (arg == "--process-timeout") options.processTimeoutMs = positive(value());
            else if (arg == "--max-nodes") options.maxNodes = positive(value());
            else if (arg == "--max-steps") options.maxSteps = positive(value());
            else if (arg == "--max-elements") options.maxElements = positive(value());
            else if (arg == "--max-bits") {
                auto bits=positive(value());
                if(bits>std::numeric_limits<unsigned>::max()) throw vx::Error({},"bit budget exceeds supported option range");
                options.maxBits=static_cast<unsigned>(bits);
            }
            else if (arg == "--max-decisions") options.maxDecisions = positive(value());
            else if (arg == "--max-checks") options.maxConstraintChecks = positive(value());
            else if (arg == "--branch-order") options.branchOrder = value();
            else if (arg == "--heuristic") options.heuristicProgram = value();
            else if (arg == "--heuristic-replay") options.heuristicReplay = value();
            else if (arg == "--max-proposals") options.maxProposals = positive(value());
            else if (arg == "--heuristic-timeout") options.heuristicTimeoutMs = positive(value());
            else if (arg == "--compiled-search") options.compiledSearch = value();
            else if (arg == "--verify-search-kernel") options.verifySearchKernel = true;
            else if (!arg.empty() && arg[0] == '-') throw vx::Error({}, "unknown option: " + arg);
            else if (input.empty()) input = arg;
            else throw vx::Error({}, "only one root source file is accepted");
        }
        if (input.empty()) { usage(); return 2; }
        if (options.schedule != "fifo" && options.schedule != "lifo" && options.schedule != "random")
            throw vx::Error({}, "schedule must be fifo, lifo, or random");
        if (options.branchOrder != "forward" && options.branchOrder != "reverse" && options.branchOrder != "random")
            throw vx::Error({}, "branch order must be forward, reverse, or random");
        if (!args.empty() && call.empty()) throw vx::Error({}, "--arg requires --call");
        if (!options.heuristicProgram.empty() && !options.heuristicReplay.empty())
            throw vx::Error({}, "choose a heuristic provider or offline replay, not both");
        if (options.verifySearchKernel && options.compiledSearch.empty())
            throw vx::Error({}, "--verify-search-kernel requires --compiled-search");
        if (!roots.empty() && !call.empty()) throw vx::Error({}, "choose --root or --call");
        if (msx.directory.empty() && msxOption) throw vx::Error({}, "MSX options require --emit-msx");
        if (!msx.directory.empty() && (!roots.empty() || !call.empty()))
            throw vx::Error({}, "--emit-msx uses exported roots; do not combine with --root/--call");
        graph.options = options;
        vx::loadProgram(graph, input);
        if (!msx.directory.empty()) vx::installMsxFacts(graph, msx);
        vx::Solver solver(graph);
        if (!call.empty()) solver.call(call, args);
        else if (!roots.empty()) for (const auto& name : roots) solver.root(name);
        else solver.exportRoots();
        solver.solve();
        if (!searchFile.empty()) write(searchFile, vx::searchReport(graph));
        if (!msx.directory.empty()) vx::exportMsx(graph, solver.roots, msx);
        if (!graphFile.empty()) write(graphFile, solver.dot());
        if (!depFile.empty()) write(depFile, solver.depfile(target));
        std::cout << solver.report(json);
        return 0;
    } catch (const vx::Error& error) {
        if (!searchFile.empty()) {
            try { write(searchFile, vx::searchReport(graph,&error)); }
            catch (const std::exception& reportError) { std::cerr << reportError.what() << '\n'; }
        }
        std::cerr << graph.location(error.span) << ": error: " << error.what() << '\n'; return 1;
    } catch (const std::exception& error) {
        std::cerr << "vexel: internal error: " << error.what() << '\n'; return 2;
    }
}
