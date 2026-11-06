#include "vexel.hpp"
#include "constraints.hpp"
#include <sstream>

namespace vx {
std::string searchReport(const Graph& graph,const Error* error) {
    const char* status="resolved";
    if(error) {
        switch(error->category) {
        case Diagnostic::Unresolved: status="unresolved";break;
        case Diagnostic::Exhausted: status="exhausted";break;
        case Diagnostic::SearchAmbiguous: status="ambiguous";break;
        case Diagnostic::InvalidReplay: status="invalid-replay";break;
        default: status="rejected";break;
        }
    }
    std::ostringstream out; out << "{\"format\":\"vexel-search-1\",\"status\":" << quote(status) << ",\"work\":{\"decisions\":"
        << (graph.options.searchBudget ? graph.options.searchBudget->decisions : 0) << ",\"tuple_checks\":"
        << (graph.options.searchBudget ? graph.options.searchBudget->checks : 0) << "},\"puzzles\":[";
    bool first = true;
    for (const auto& [id, problem] : graph.puzzles) {
        if (!first) out << ',';
        first = false;
        out << "{\"source\":" << quote(graph.location(graph.nodes[id].span)) << ",\"search\":" << problem->json() << '}';
    }
    out << "],\"advice\":[";first=true;
    if(graph.options.searchBudget) for(const auto& [context,records]:graph.options.searchBudget->advice)
        for(const auto& record:records) {
            if(!first) out << ',';
            first=false;
            out << "{\"context\":" << quote(context) << ",\"request\":" << quote(record.request)
                << ",\"response\":" << quote(record.response) << ",\"status\":" << quote(record.status) << '}';
        }
    return out.str() + "]}\n";
}
std::string Solver::report(bool json) const {
    std::ostringstream out;
    std::size_t demanded = 0, residual = 0;
    for (const auto& node : g.nodes) {
        if (node.demanded) ++demanded;
        if (node.demanded && node.fact && (!node.fact->value.known() || node.fact->effect >= Effect::Control)) ++residual;
    }
    if (!json) {
        for (const auto& root : roots) {
            const auto& f = *g.nodes[root.node].fact;
            out << root.name << " : " << f.type.str() << " = " << formatValue(f.type,f.value);
            if (f.effect == Effect::External) out << " [external effects retained]";
            else if (f.effect == Effect::Control) out << " [runtime control retained]";
            out << '\n';
        }
        out << "resolved " << demanded << " obligations; " << invocations << " invocation instances; "
            << cacheHits << " pure reuse(s); " << g.dependencies.size() << " input file(s)\n";
        return out.str();
    }
    out << "{\"format\":\"vexel-resolution-1\",\"roots\":[";
    bool first = true;
    for (const auto& root : roots) {
        if (!first) out << ',';
        first = false;
        const auto& f = *g.nodes[root.node].fact;
        out << "{\"name\":" << quote(root.name) << ",\"node\":" << root.node
            << ",\"type\":" << quote(f.type.str()) << ",\"known\":" << (f.value.known() ? "true" : "false")
            << ",\"value\":" << quote(formatValue(f.type,f.value)) << ",\"effect\":" << quote(effectName(f.effect)) << "}";
    }
    out << "],\"type_equations\":[";first=true;
    for(const auto& [owner,check]:equationChecks) {
        if(!first) out << ',';
        first=false;
        const auto& node=g.nodes[owner];
        const auto& candidate=g.nodes[node.rule==Rule::Global ? node.signature : node.annotation].fact->type;
        const auto& proof=g.nodes[check].fact;
        out << "{\"name\":" << quote(g.nodes[node.site].text)
            << ",\"source\":" << quote(g.location(node.span))
            << ",\"owner\":" << owner << ",\"check\":" << check
            << ",\"candidate\":" << quote(candidate.str())
            << ",\"verified\":" << (proof && proof->type==candidate ? "true" : "false") << '}';
    }
    out << "],\"type_properties\":[";first=true;
    for(const auto& [key,proof]:propertyProofs) {
        if(!first) out << ',';
        first=false;
        const auto& node=g.nodes[key.first];
        const auto& result=g.nodes[proof.check].fact;
        out << "{\"name\":" << quote(g.nodes[node.site].text)
            << ",\"source\":" << quote(g.location(node.span))
            << ",\"owner\":" << key.first << ",\"path\":[";
        for(std::size_t i=0;i<key.second.size();++i) {if(i) out << ',';out << key.second[i];}
        out << "],\"witness\":" << propertyNodes.at(key).witness
            << ",\"check\":" << proof.check << ",\"candidate\":" << quote(proof.candidate.str())
            << ",\"verified\":" << (result && result->type==proof.candidate ? "true" : "false") << '}';
    }
    out << "],\"type_aliases\":[";first=true;
    for(const auto& [owner,proof]:typeAliasProofs) {
        if(!first) out << ',';
        first=false;
        out << "{\"owner\":" << owner << ",\"target\":" << proof.target
            << ",\"requirement\":" << proof.requirement << ",\"join\":" << proof.join
            << ",\"proof\":\"opaque-type-equality\"}";
    }
    out << "],\"annotations\":[";first=true;
    for (const auto& n : g.nodes) if (n.syntax!=Syntax::None && !n.metadata.empty()) {
        if (!first) out << ',';
        first=false;
        out << "{\"site\":" << quote(n.identity) << ",\"source\":" << quote(g.location(n.span)) << ",\"text\":[";
        for (std::size_t i=0;i<n.metadata.size();++i) {if(i)out<<',';out<<quote(n.metadata[i]);}
        out << "]}";
    }
    out << "],\"process_inputs\":[";first=true;
    for (const auto& [request,result] : *g.processSnapshots) {
        if (!first) out << ',';
        first=false;out << "{\"request\":" << quote(request) << ",\"status\":" << quote(result.second) << ",\"stdout\":" << quote(result.first) << '}';
    }
    out << "],\"dependencies\":["; first = true;
    for (const auto& path : g.dependencies) {
        if (!first) out << ',';
        first = false; out << quote(path);
    }
    out << "],\"stats\":{\"nodes\":" << g.nodes.size() << ",\"demanded\":" << demanded << ",\"residual\":" << residual
        << ",\"steps\":" << steps << ",\"invocations\":" << invocations << ",\"cache_hits\":" << cacheHits << "},\"nodes\":[";
    first = true;
    auto edges = [&](const std::string& name, const std::vector<Id>& ids) {
        out << ',' << quote(name) << ":[";
        for (std::size_t i = 0; i < ids.size(); ++i) { if (i) out << ','; out << ids[i]; }
        out << ']';
    };
    for (Id id = 0; id < g.nodes.size(); ++id) {
        const auto& n = g.nodes[id]; if (n.rule == Rule::None) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << id << ",\"site\":" << quote(n.identity)
            << ",\"rule\":" << quote(ruleName(n.rule)) << ",\"operator\":" << quote(n.text)
            << ",\"source\":" << quote(g.location(n.span))
            << ",\"demanded\":" << (n.demanded ? "true" : "false")
            << ",\"type_demanded\":" << (n.typeDemanded ? "true" : "false");
        if (n.fact) out << ",\"type\":" << quote(n.fact->type.str()) << ",\"value\":" << quote(formatValue(n.fact->type,n.fact->value))
            << ",\"known\":" << (n.fact->value.known() ? "true" : "false") << ",\"effect\":" << quote(effectName(n.fact->effect));
        else if (n.inferredType) out << ",\"type\":" << quote(n.inferredType->str()) << ",\"type_only\":true";
        if (n.body != none && n.rule == Rule::Invoke) out << ",\"callee\":" << quote(g.identity(n.body));
        if (n.expansion != none) out << ",\"expansion\":" << n.expansion;
        if (n.previous != none) out << ",\"before\":" << n.previous;
        if (n.endState != none) out << ",\"exit_state\":" << n.endState;
        if (n.slot != none) out << ",\"slot\":" << n.slot;
        if (n.region != none) out << ",\"region\":" << n.region;
        if (n.runtimeBackedge != none) out << (n.signatureOrigin == none ? ",\"runtime_backedge\":" : ",\"signature_backedge\":") << n.runtimeBackedge;
        if (n.signature != none) out << ",\"signature\":" << n.signature;
        if (n.signatureOrigin != none) out << ",\"signature_origin\":" << n.signatureOrigin;
        if (!n.carried.empty()) { edges("carried", n.carried); edges("seeds", n.seeds); }
        if (!n.environment.empty()) edges("environment",n.environment);
        edges("inputs", n.inputs); edges("requires", n.dependencies);
        if (!n.typeDependencies.empty()) edges("requires_type", n.typeDependencies);
        if(n.fact || n.inferredType) {
            std::set<Id> fields,elements,schemas;
            std::function<void(const Type&,bool)> pending=[&](const Type& type,bool element) {
                if(type.kind==Type::Unknown && type.obligation!=none) (element ? elements : fields).insert(type.obligation);
                if(type.schemaObligation!=none) schemas.insert(type.schemaObligation);
                if(type.kind==Type::Array) pending(*type.element,true);
                for(const Type& field:type.fields) pending(field,false);
            };
            pending(n.fact ? n.fact->type : *n.inferredType,false);
            if(!fields.empty()) edges("field_type_obligations",std::vector<Id>(fields.begin(),fields.end()));
            if(!elements.empty()) edges("element_type_obligations",std::vector<Id>(elements.begin(),elements.end()));
            if(!schemas.empty()) edges("schema_obligations",std::vector<Id>(schemas.begin(),schemas.end()));
        }
        if (!n.branches.empty()) edges("branches", n.branches);
        if (!n.ends.empty()) edges("branch_states", n.ends);
        if (!n.completion.normal || !n.completion.exits.empty()) {
            out << ",\"normal_completion\":" << (n.completion.normal ? "true" : "false");
            edges("completion_exits",n.completion.exits);
        }
        out << '}';
    }
    out << "],\"puzzles\":[";
    first = true;
    for (const auto& [id, problem] : g.puzzles) {
        if (!first) out << ',';
        first = false;
        out << "{\"node\":" << id << ",\"search\":" << problem->json() << '}';
    }
    out << "]}\n"; return out.str();
}
std::string Solver::dot() const {
    std::ostringstream out;
    out << "digraph resolution {\n  rankdir=LR;\n  node [shape=box,fontname=\"monospace\"];\n";
    for (Id id = 0; id < g.nodes.size(); ++id) {
        const auto& n = g.nodes[id]; if (!n.demanded) continue;
        std::string label = ruleName(n.rule) + (n.text.empty() ? "" : " " + n.text) + "\n" + g.location(n.span);
        if (n.fact) {
            std::string value = n.fact->value.str();
            if (value.size() > 100) value = value.substr(0, 100) + "...";
            label += "\n" + n.fact->type.str() + " = " + value;
        }
        out << "  n" << id << " [label=" << quote(label);
        if (n.fact && n.fact->effect >= Effect::Control) out << ",color=orange";
        else if (n.fact && !n.fact->value.known()) out << ",color=blue";
        out << "];\n";
        for (Id dep : n.dependencies) out << "  n" << id << " -> n" << dep << ";\n";
        if (n.runtimeBackedge != none)
            out << "  n" << n.runtimeBackedge << " -> n" << id
                << " [style=dashed,color=blue,label=\"next iteration (runtime)\"];\n";
    }
    return out.str() + "}\n";
}
std::string Solver::depfile(const std::string& target) const {
    auto escape = [](const std::string& text) {
        std::string escaped;
        for (char c : text) {
            if (c == '\n' || c == '\r') throw Error({}, "Make dependency paths cannot contain newlines");
            if (c == '$') escaped += '$';
            if (c == ' ' || c == '\t' || c == '#' || c == '\\' || c == ':') escaped += '\\';
            escaped += c;
        }
        return escaped;
    };
    std::string result = escape(target) + ":";
    for (const auto& path : g.dependencies) result += " " + escape(path);
    result+='\n';
    // Like compiler -MP output, empty prerequisite rules allow deletion to
    // trigger another resolution instead of stopping Make before it can run.
    for(const auto& path:g.dependencies) if(path!=target) result+=escape(path)+":\n";
    return result;
}
} // namespace vx
