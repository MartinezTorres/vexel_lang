#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include "constraints.hpp"
#include "process.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace vx {
namespace {
using Tree = boost::property_tree::ptree;
// This is an identity check, not an authentication mechanism. Proposals are
// checked against current domains regardless of their identity string.
std::string fingerprint(const std::string& text) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
    std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << h; return out.str();
}
Tree parse(const std::string& text) {
    unsigned depth = 0; bool quoted = false, escaped = false;
    for (char c : text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 128) throw std::runtime_error("JSON nesting budget exhausted"); }
        else if (c == '}' || c == ']') { if (!depth) throw std::runtime_error("invalid JSON delimiters"); --depth; }
    }
    std::istringstream stream(text); Tree tree;
    boost::property_tree::read_json(stream, tree); return tree;
}

}
void ConstraintProblem::prepareAdvisor() {
    proposals.clear(); replay.clear(); problemKey.clear();
    if (options.heuristicProgram.empty() && options.heuristicReplay.empty()) return;
    std::ostringstream identity;
    identity << "vexel-relations-1;";
    for (const auto& v : variables) {
        identity << quote(v.name) << ':' << v.decision << '[';
        for (const auto& value : v.values) identity << value << ',';
        identity << ']';
    }
    for (const auto& r : relations) {
        identity << quote(r.label) << '[';
        for (Id v : r.variables) identity << v << ',';
        identity << ']';
        for (const auto& row : r.tuples) { for (auto v : row) identity << v << ','; identity << ';'; }
    }
    problemKey = fingerprint(identity.str());
    if (options.heuristicReplay.empty()) return;
    try {
        std::ifstream stream(options.heuristicReplay, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot read heuristic replay");
        std::string bytes; char chunk[8192];
        while (stream.read(chunk, sizeof(chunk)) || stream.gcount()) {
            bytes.append(chunk, static_cast<std::size_t>(stream.gcount()));
            if (bytes.size() > options.maxFileBytes) throw std::runtime_error("heuristic replay exceeds file budget");
        }
        Tree report = parse(bytes);
        if (report.get<std::string>("format") != "vexel-search-1") throw std::runtime_error("invalid heuristic replay format");
        if(auto advice=report.get_child_optional("advice")) for(const auto& item:*advice) {
            std::string request=item.second.get<std::string>("request");
            std::string response=item.second.get<std::string>("response");
            if(request.size()<=65536 && response.size()<=65536) replay.emplace(std::move(request),std::move(response));
        }
        for (const auto& puzzle : report.get_child("puzzles"))
            for (const auto& item : puzzle.second.get_child("search.proposals")) {
                std::string request = item.second.get<std::string>("request");
                std::string response = item.second.get<std::string>("response");
                if (request.size() <= 65536 && response.size() <= 65536) replay.emplace(std::move(request), std::move(response));
            }
    } catch (const std::exception& error) { throw Error({}, std::string("invalid heuristic replay: ") + error.what(),Diagnostic::InvalidReplay); }
}
std::optional<std::pair<Id, std::size_t>> ConstraintProblem::advise(std::size_t depth, Id fallback,const std::string& context) {
    if(problemKey.empty()) return {};
    const std::string owner=advisorContext.empty() ? problemKey : advisorContext;
    const std::size_t sequence=options.searchBudget ? options.searchBudget->advice[owner].size() : proposals.size();
    if (sequence >= options.maxProposals) return {};
    auto remember=[&](Proposal proposal) {
        if(options.searchBudget) options.searchBudget->advice[owner].push_back(proposal);
        proposals.push_back(std::move(proposal));
    };
    std::ostringstream state;
    state << "{\"format\":\"vexel-branch-1\",\"problem\":" << quote(problemKey)
          << ",\"depth\":" << depth << ",\"fallback\":" << fallback
          << ",\"sequence\":" << sequence << ",\"kind\":" << quote(context.empty() ? "numeric" : "structural");
    if(!context.empty()) state << ",\"context\":" << context;
    state << ",\"variables\":[";
    bool first = true;
    for (Id id = 0; id < variables.size(); ++id) {
        const auto& v = variables[id];
        if (v.remaining <= 1) continue;
        if (!first) state << ',';
        first = false;
        state << "{\"id\":" << id << ",\"name\":" << quote(v.name) << ",\"choice\":" << (v.decision ? "true" : "false") << ",\"domain\":[";
        bool comma = false;
        for (std::size_t i = 0; i < v.values.size(); ++i) if (v.active[i]) {
            if (comma) state << ',';
            comma = true; state << quote(v.values[i].str());
        }
        state << "]}";
        if (state.tellp() > 65536) { event("heuristic-skipped", depth, fallback, "request-too-large"); return {}; }
    }
    state << "]}";
    std::string id = fingerprint(state.str());
    std::string request = state.str(); request.pop_back(); request += ",\"id\":" + quote(id) + "}";
    if (request.size() > 65536) { event("heuristic-skipped", depth, fallback, "request-too-large"); return {}; }
    Proposal proposal{request, {}, {}};
    if (!options.heuristicReplay.empty()) {
        auto found = replay.find(request);
        if (found == replay.end()) { proposal.status = "replay-miss"; remember(std::move(proposal)); return {}; }
        proposal.response = found->second; proposal.status = "replayed";
    } else {
        auto [response, status] = runProcess({options.heuristicProgram}, request, options.heuristicTimeoutMs);
        proposal.response = std::move(response); proposal.status = std::move(status);
        if (proposal.status != "received") { remember(std::move(proposal)); return {}; }
    }
    std::optional<std::pair<Id, std::size_t>> recommendation;
    try {
        Tree response = parse(proposal.response);
        std::set<std::string> keys;
        for (const auto& field : response) {
            if (!keys.insert(field.first).second || !field.second.empty() ||
                (field.first != "id" && field.first != "variable" && field.first != "value"))
                throw std::runtime_error("invalid response field");
        }
        if (keys.size() != 3 || response.get<std::string>("id") != id) throw std::runtime_error("stale request");
        std::string index = response.get<std::string>("variable");
        if (index.empty() || index.size() > 16 || index.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid variable");
        Id variable = static_cast<Id>(std::stoull(index));
        if (variable >= variables.size() || variables[variable].remaining < 2) throw std::runtime_error("variable is not a decision");
        auto& v = variables[variable]; const std::string value = response.get<std::string>("value");
        for (std::size_t i = 0; i < v.values.size(); ++i)
            if (v.active[i] && v.values[i].str() == value) { recommendation = {{variable, i}}; break; }
        if (!recommendation) throw std::runtime_error("value not in domain");
        proposal.status = options.heuristicReplay.empty() ? "accepted" : "replayed";
    } catch (const std::exception&) { proposal.status = "rejected"; }
    event("heuristic", depth, fallback, proposal.status);
    remember(std::move(proposal)); return recommendation;
}
std::optional<Big> ConstraintProblem::adviseStructure(const Graph& graph,const ChoiceRequired& request) {
    if(options.heuristicProgram.empty() && options.heuristicReplay.empty()) return {};
    // The relation graph may still be incomplete. Include the source/input
    // revision, target facts and active structural assumptions in its identity.
    std::ostringstream revision;
    revision << "vexel-structure-1;" << graph.nodes[request.context].identity << ';' << options.strictness << ';';
    for(const auto& file:graph.files) revision << quote(file.path.string()) << ':' << fingerprint(file.bytes) << ';';
    for(const auto& [path,bytes]:*graph.inputSnapshots) revision << quote(path) << ':' << fingerprint(bytes) << ';';
    for(const auto& [path,entries]:*graph.directorySnapshots) {
        revision << quote(path) << '[';for(const auto& entry:entries) revision << quote(entry) << ';';revision << ']';
    }
    for(const auto& [command,snapshot]:*graph.processSnapshots)
        revision << quote(command) << ':' << fingerprint(snapshot.first) << ':' << fingerprint(snapshot.second) << ';';
    for(const auto& [name,id]:graph.targetFacts) {
        const auto& fact=graph.nodes[id].fact;
        revision << quote(name) << ':';
        if(fact) revision << quote(fact->type.str()) << ':' << quote(fact->value.str());
    }
    std::ostringstream context;
    const auto& source=graph.nodes[request.context].span;
    context << "{\"source\":" << quote(graph.location(source));
    if(source.file!=none) {
        const auto& bytes=graph.files[source.file].bytes;
        context << ",\"expression\":" << quote(bytes.substr(source.begin,source.end-source.begin));
    }
    context << ",\"revision\":" << quote(fingerprint(revision.str())) << ",\"assumptions\":[";
    bool first=true;
    for(const auto& [name,value]:graph.options.structuralAssumptions) {
        if(!first) context << ',';
        first=false;
        context << "{\"name\":" << quote(name) << ",\"value\":" << quote(value.str()) << '}';
    }
    context << "]}";
    // Restrict advice to this already requested structural decision. The
    // ordinary resolver still validates every alternative and every result.
    ConstraintProblem advisor(options,advisorContext);
    advisor.variable(request.name,request.domain,true);advisor.prepareAdvisor();
    advisor.problemKey=fingerprint(advisor.problemKey+context.str());
    auto preferred=advisor.advise(graph.options.structuralAssumptions.size(),0,context.str());
    proposals.insert(proposals.end(),advisor.proposals.begin(),advisor.proposals.end());
    for(auto item:advisor.events) if(events.size()<options.maxElements) {
        item.kind+="-structure";item.subject=none;events.push_back(std::move(item));
    }
    if(!preferred) return {};
    return advisor.variables[preferred->first].values[preferred->second];
}
} // namespace vx
