#include "vexel.hpp"
#include "floating.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace vx {
Type Type::integer(unsigned width, bool isSigned) {
    Type t; t.kind = Integer; t.bits = width; t.sign = isSigned; return t;
}
Type Type::fixed(unsigned integerBits,unsigned fractionalBits,bool isSigned) {
    Type t; t.kind = Fixed; t.bits = integerBits + fractionalBits;
    t.fraction = fractionalBits; t.sign = isSigned; return t;
}
Type Type::array(Type item, std::size_t count) {
    Type t; t.kind = Array; t.size = count;
    t.element = std::make_shared<Type>(std::move(item)); return t;
}
Type Type::record(Id declaration, std::string name, std::vector<std::string> names, std::vector<Type> fields) {
    Type t; t.kind = Record; t.declaration = declaration; t.name = std::move(name);
    t.fieldNames = std::move(names); t.fields = std::move(fields); t.size = t.fields.size(); return t;
}
bool Type::concrete() const {
    return kind != Unknown && kind != Exact && kind != Empty &&
        (kind != Array || element->concrete()) &&
        (kind != Record || (schemaObligation==none && std::all_of(fields.begin(), fields.end(), [](const Type& t) { return t.concrete(); })));
}
bool Type::settled() const {
    return kind != Unknown && (kind != Array || element->settled()) &&
        (kind != Record || (schemaObligation==none && std::all_of(fields.begin(), fields.end(), [](const Type& t) { return t.settled(); })));
}
bool Type::operator==(const Type& b) const {
    return kind == b.kind && bits == b.bits && fraction == b.fraction && sign == b.sign &&
        size == b.size && (kind != Unknown || obligation == b.obligation) && (kind != Array || *element == *b.element) &&
        (kind != Record || (declaration == b.declaration && schemaObligation==b.schemaObligation && schema == b.schema && fields == b.fields && fieldNames == b.fieldNames));
}
std::string Type::str() const {
    switch (kind) {
    case Unknown: return "?";
    case Empty: return "(no value)";
    case Exact: return "#integer";
    case Integer: return std::string(sign ? "#i" : "#u") + std::to_string(bits);
    case Fixed: return std::string(sign ? "#i" : "#u") + std::to_string(bits-fraction) + "." + std::to_string(fraction);
    case Float: return "#f" + std::to_string(bits);
    case Boolean: return "#b";
    case String: return "#s";
    case Array: return element->str() + "[" + std::to_string(size) + "]";
    case Record: {
        std::string out = name.empty() ? "(" : "#" + name + "(";
        for (std::size_t i = 0; i < fields.size(); ++i) { if (i) out += ","; out += fields[i].str(); }
        return out + ")" + (schema.empty() ? "" : "{"+schema+"}");
    }
    }
    throw std::logic_error("invalid type");
}
Value Value::integer(Big n) { Value v; v.kind = Number; v.number = std::move(n); return v; }
Value Value::string(std::string s) { Value v; v.kind = Bytes; v.bytes = std::move(s); return v; }
Value Value::array(std::vector<Value> a) { Value v; v.kind = Elements; v.elements = std::move(a); return v; }
Value Value::runtime() { Value v; v.kind = Runtime; return v; }
Value Value::symbolic(Id owner, Id id) { Value v; v.kind = Symbolic; v.context = owner; v.variable = id; return v; }
bool Value::symbolic() const {
    return kind == Symbolic || (kind == Elements &&
        std::any_of(elements.begin(), elements.end(), [](const Value& v) { return v.symbolic(); }));
}
bool Value::known() const {
    return kind != Runtime && kind != Symbolic && (kind != Elements ||
        std::all_of(elements.begin(), elements.end(), [](const Value& v) { return v.known(); }));
}
bool Value::operator==(const Value& b) const {
    return kind == b.kind && number == b.number && bytes == b.bytes && elements == b.elements &&
        context == b.context && variable == b.variable;
}
std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 32 || c >= 127)
            out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
        else out << c;
    }
    return out.str() + '"';
}
std::string Value::str() const {
    switch (kind) {
    case Empty: return "(no value)";
    case Runtime: return "runtime";
    case Symbolic: return "choice@" + std::to_string(context) + ":" + std::to_string(variable);
    case Number: return number.str();
    case Bytes: return quote(bytes);
    case Elements: {
        std::string out = "[";
        for (const auto& e : elements) { if (out.size() > 1) out += ", "; out += e.str(); }
        return out + "]";
    }
    }
    throw std::logic_error("invalid value");
}
std::string formatValue(const Type& type,const Value& value) {
    if (value.kind == Value::Elements) {
        std::string out = "[";
        for (std::size_t i=0;i<value.elements.size();++i) {
            if (i) out += ", ";
            out += formatValue(type.kind == Type::Array ? *type.element : type.fields.at(i),value.elements[i]);
        }
        return out + "]";
    }
    if (type.kind == Type::Float && value.kind == Value::Number) return formatFloat(value.number,type.bits);
    if (type.kind != Type::Fixed || value.kind != Value::Number) return value.str();
    Big magnitude = value.number < 0 ? -value.number : value.number;
    Big scale = Big(1) << type.fraction;
    std::string result = (value.number < 0 ? "-" : "") + Big(magnitude/scale).str();
    Big remainder = magnitude % scale;
    if (remainder != 0) result += '.';
    while (remainder != 0) {
        remainder *= 10; result += static_cast<char>('0' + Big(remainder/scale).convert_to<unsigned>());
        remainder %= scale;
    }
    return result;
}
Id Graph::add(Node n) {
    if (nodes.size() >= options.maxNodes) throw Error(n.span, "graph node budget exhausted (--max-nodes)",Diagnostic::Exhausted);
    Id id = nodes.size();
    if (n.identity.empty()) n.identity = location(n.span) + ":" + std::to_string(n.span.end);
    nodes.push_back(std::move(n)); return id;
}
Id Graph::semantic(Rule r, Id site, std::vector<Id> inputs, Id scopeId) {
    Node n; n.rule = r; n.site = site; n.inputs = std::move(inputs); n.scope = scopeId;
    if (site != none) { n.span = nodes.at(site).span; n.metadata = nodes.at(site).metadata; }
    n.identity = (scopeId == none ? "" : nodes[scopes.at(scopeId).node].identity + "/") +
        identity(site) + ":" + ruleName(r);
    return add(std::move(n));
}
Id Graph::scope(Id parent, Id site, const std::string& name) {
    Node n; n.text = "scope"; n.identity = name;
    if (site != none) n.span = nodes[site].span;
    Scope s; s.node = add(std::move(n)); s.parent = parent;
    if (parent != none) {
        s.puzzle = scopes.at(parent).puzzle; s.guard = scopes.at(parent).guard;
        s.function = scopes.at(parent).function; s.loop = scopes.at(parent).loop;
        s.runtimeControl = scopes.at(parent).runtimeControl;
        s.typeValues = scopes.at(parent).typeValues;
        s.modulePath = scopes.at(parent).modulePath;
    }
    if (site!=none && (nodes[site].syntax==Syntax::Function || nodes[site].syntax==Syntax::Block)) s.modulePath+=identity(site)+"/";
    scopes.push_back(std::move(s)); return scopes.size() - 1;
}
Id Graph::read(const std::filesystem::path& requested, bool source, Span requester) {
    std::error_code ec;
    const auto selection="path-target:"+std::filesystem::absolute(requested).lexically_normal().string();
    auto selected=inputSnapshots->find(selection);
    auto path=selected==inputSnapshots->end() ? std::filesystem::weakly_canonical(requested,ec) : std::filesystem::path(selected->second);
    if (ec) throw Error(requester, "cannot resolve file: " + requested.string());
    if(selected==inputSnapshots->end()) inputSnapshots->emplace(selection,path.string());
    const std::string key = path.string() + (source ? ":source" : ":resource");
    if (auto it = loaded.find(key); it != loaded.end()) return it->second;
    std::string bytes;
    if (auto found = inputSnapshots->find(path.string()); found != inputSnapshots->end()) bytes = found->second;
    else {
    std::ifstream input(path, std::ios::binary);
    if (!input || !std::filesystem::is_regular_file(path, ec))
        throw Error(requester, "cannot read file: " + path.string());
    char chunk[8192];
    while (input) {
        input.read(chunk, sizeof chunk);
        bytes.append(chunk, static_cast<std::size_t>(input.gcount()));
        if (bytes.size() > options.maxFileBytes) throw Error(requester, "file size budget exhausted: " + path.string(),Diagnostic::Exhausted);
    }
    if (!input.eof()) throw Error(requester, "failed reading file: " + path.string());
        inputSnapshots->emplace(path.string(),bytes);
    }
    Id id = files.size();
    Node n; n.syntax = Syntax::File; n.span = {id, 0, bytes.size()}; n.identity = path.string();
    files.push_back({path, std::move(bytes), source, none});
    files[id].node = add(std::move(n));
    loaded[key] = id; dependencies.insert(path.string());return id;
}
std::filesystem::path Graph::resolve(const std::string& path, Span from) {
    // Search precedence is an input too. If a preferred module is absent,
    // creating it must invalidate a build which currently uses a fallback.
    auto present=[&](const std::filesystem::path& candidate) {
        const auto key="path-exists:"+std::filesystem::absolute(candidate).lexically_normal().string();
        if(auto found=inputSnapshots->find(key);found!=inputSnapshots->end()) return found->second=="true";
        std::error_code error;
        bool exists=std::filesystem::exists(candidate,error);
        if(error) throw Error(from,"cannot inspect input path: "+candidate.string());
        // Even an absent leaf has a selected destination. Retargeting a
        // symlinked parent changes that premise while exists() stays false.
        auto resolved=std::filesystem::weakly_canonical(candidate,error);
        if(error) throw Error(from,"cannot resolve input path: "+candidate.string());
        inputSnapshots->emplace("path-target:"+std::filesystem::absolute(candidate).lexically_normal().string(),resolved.string());
        if(!exists) {
            auto parent=candidate.parent_path();
            while(!std::filesystem::is_directory(parent,error) && parent.has_parent_path() && parent!=parent.parent_path()) parent=parent.parent_path();
            if(error) throw Error(from,"cannot inspect input directory: "+parent.string());
            parent=std::filesystem::weakly_canonical(parent,error);
            if(error) throw Error(from,"cannot resolve input directory: "+parent.string());
            dependencies.insert(parent.string());
        }
        inputSnapshots->emplace(key,exists ? "true" : "false");return exists;
    };
    auto candidate=project/path;
    if(present(candidate)) return candidate;
    if(path.starts_with("std/")) candidate=std::filesystem::path(VEXEL_LIBRARY_DIR)/path;
    else if(from.file!=none) candidate=files.at(from.file).path.parent_path()/path;
    (void)present(candidate);
    return candidate;
}
std::string Graph::location(Span s) const {
    if (s.file == none) return "<request>";
    const auto& f = files.at(s.file);
    std::size_t line = 1, column = 1;
    for (std::size_t i = 0; i < s.begin && i < f.bytes.size(); ++i) {
        if (f.bytes[i] == '\n') { ++line; column = 1; } else ++column;
    }
    return f.path.string() + ":" + std::to_string(line) + ":" + std::to_string(column);
}
std::string Graph::identity(Id site) const { return site == none ? "<request>" : nodes.at(site).identity; }
std::string effectName(Effect e) {
    return e == Effect::Pure ? "pure" : e == Effect::Local ? "local" : e == Effect::Control ? "control" : "external";
}
std::string ruleName(Rule r) {
    switch (r) {
#define VX_RULE(x) case Rule::x: return #x;
    VX_RULE(None) VX_RULE(Constant) VX_RULE(Input) VX_RULE(Type) VX_RULE(Global)
    VX_RULE(GlobalRead) VX_RULE(Bind) VX_RULE(Unary) VX_RULE(Binary) VX_RULE(ShortCircuit) VX_RULE(Select)
    VX_RULE(Array) VX_RULE(Index) VX_RULE(Cast) VX_RULE(Invoke) VX_RULE(Finish)
    VX_RULE(Entry) VX_RULE(Slot) VX_RULE(Store) VX_RULE(Load) VX_RULE(Step)
    VX_RULE(StateOf) VX_RULE(Merge) VX_RULE(Iterate) VX_RULE(Resource) VX_RULE(Adapt)
    VX_RULE(Update) VX_RULE(Loop) VX_RULE(LoopHeader) VX_RULE(LoopExit)
    VX_RULE(LoopValue) VX_RULE(LoopItem) VX_RULE(Repeat) VX_RULE(Sort)
    VX_RULE(Solve) VX_RULE(Choice) VX_RULE(Require)
    VX_RULE(Construct) VX_RULE(FieldIndex) VX_RULE(Tuple) VX_RULE(ShapeCheck)
    VX_RULE(Probe)
    VX_RULE(Deferred)
    VX_RULE(Alias) VX_RULE(CallProbe)
    VX_RULE(Sequence) VX_RULE(Exit) VX_RULE(FunctionResult) VX_RULE(Resume) VX_RULE(ExitJoin)
    VX_RULE(CallState)
    VX_RULE(Unpack)
    VX_RULE(Evaluate)
    VX_RULE(CustomIteration) VX_RULE(IterationBody) VX_RULE(TypeJoin)
    VX_RULE(PropertyLoad) VX_RULE(PropertyValue) VX_RULE(PropertyCall)
    VX_RULE(Math)
#undef VX_RULE
    }
    throw std::logic_error("invalid rule");
}
} // namespace vx
