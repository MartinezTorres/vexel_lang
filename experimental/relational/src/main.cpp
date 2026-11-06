#include "machine.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c < 32) throw std::invalid_argument("control character in report");
        else result += static_cast<char>(c);
    }
    return result + '"';
}
}
int main(int argc, char** argv) {
    using namespace vx::relational;
    try {
        std::string mode = "overlap", output;
        unsigned budget = 6; Options options;
        for (int i = 1; i < argc; ++i) {
            std::string flag = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc) throw std::invalid_argument("missing value for " + flag);
                return argv[i];
            };
            if (flag == "--mode") mode = value();
            else if (flag == "--budget") budget = static_cast<unsigned>(std::stoul(value()));
            else if (flag == "--schedule") options.schedule = value();
            else if (flag == "--branch-order") options.branches = value();
            else if (flag == "--max-work") options.maxWork = std::stoul(value());
            else if (flag == "--max-decisions") options.maxDecisions = std::stoul(value());
            else if (flag == "--output") output = value();
            else throw std::invalid_argument("unknown option: " + flag);
        }
        Program program; auto demo = demonstration(program,mode,budget);
        auto result = Machine(program,options).solve(demo.request);
        std::ostringstream report;
        report << "{\"format\":\"vexel-relational-experiment-1\",\"status\":" << quote(outcomeName(result.outcome))
               << ",\"mode\":" << quote(mode) << ",\"payload_budget\":" << budget
               << ",\"stats\":{\"work\":" << result.stats.work << ",\"decisions\":" << result.stats.decisions
               << ",\"contradictions\":" << result.stats.contradictions << ",\"models\":" << result.stats.models
               << ",\"unfinished\":" << result.stats.unfinished << "},\"pending\":[";
        for (std::size_t i = 0; i < result.pending.size(); ++i) {
            if (i) report << ',';
            report << quote(result.pending[i]);
        }
        report << ']';
        // A provisional witness is not an exportable physical plan.
        if (result.outcome == Outcome::Resolved) {
            const auto& model = *result.selected;
            report << ",\"plan\":{\"buffer_type\":" << quote(show(model.realization[0]))
                   << ",\"element_bits\":" << model.realization[1]->number
                   << ",\"buffer_bytes\":" << model.realization[2]->number
                   << ",\"offsets\":[" << model.realization[3]->number << ',' << model.realization[4]->number << ']'
                   << ",\"extent\":" << model.realization[5]->number
                   << ",\"preference\":\"prefer 16-bit, then minimize occupied payload extent\",\"assumptions\":[";
            for (std::size_t i = 0; i < model.assumptions.size(); ++i) {
                if (i) report << ',';
                report << quote(model.assumptions[i]);
            }
            report << "]}";
        }
        report << "}\n";
        if (output.empty()) std::cout << report.str();
        else {
            std::ofstream file(output);
            if (!file || !(file << report.str())) throw std::runtime_error("cannot write report: " + output);
        }
        return result.outcome == Outcome::Resolved ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
