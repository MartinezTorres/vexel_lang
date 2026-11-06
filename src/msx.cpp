#include "vexel.hpp"
#include "floating.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifndef VEXEL_BACKEND_DIR
#define VEXEL_BACKEND_DIR "backends/msx-neo8"
#endif

namespace vx {
namespace {
bool scalar(const Type& t) { return t.kind == Type::Boolean || t.kind == Type::Integer || t.kind == Type::Fixed || t.kind == Type::Float; }
bool aggregateType(const Type& t) { return t.kind == Type::Array || t.kind == Type::Record; }
unsigned width(const Type& t) { return t.kind == Type::Boolean ? 1 : t.bits; }
const Type& leaf(const Type& t) { return t.kind == Type::Array ? leaf(*t.element) : t; }
unsigned elements(const Type& t) {
    if (t.kind != Type::Array) return 1;
    unsigned child = elements(*t.element);
    if (t.size > 65535 || (child && t.size > 65535 / child))
        throw Error({}, "MSX array shape exceeds addressable storage");
    return static_cast<unsigned>(t.size) * child;
}
unsigned storage(const Type& t) {
    if (t.kind == Type::Record) {
        unsigned bytes = 0;
        for (const auto& field : t.fields) {
            unsigned child = storage(field);
            if (bytes > 65535 - child) throw Error({}, "MSX record exceeds addressable storage");
            bytes += child;
        }
        return std::max(bytes, 1U);
    }
    if (t.kind != Type::Array) return width(t) <= 8 ? 1 : width(t) <= 16 ? 2 : 4;
    unsigned count = std::max(elements(t), 1U), bytes = storage(leaf(t));
    if (count > 65535 / bytes) throw Error({}, "MSX array exceeds addressable storage");
    return count * bytes;
}
std::string ctype(const Type& t) {
    if (t.kind == Type::Empty) return "void";
    if (!scalar(t) || width(t) > 32)
        throw Error({}, "MSX C backend requires scalar runtime widths 1..32, found " + t.str());
    return std::string(t.sign ? "int" : "uint") + std::to_string(storage(t) * 8) + "_t";
}
std::string mask(const Type& t) { return Big((Big(1) << width(t)) - 1).str() + "UL"; }
std::string normalize(const std::string& expression, const Type& t) {
    (void)ctype(t);
    if (t.kind == Type::Boolean) return "((uint8_t)((" + expression + ") != 0))";
    if (t.sign) return "((" + ctype(t) + ")vx_signed((uint32_t)(" + expression + "), " + std::to_string(t.bits) + "))";
    std::string value = "(uint32_t)(" + expression + ")";
    if (t.bits != 8 && t.bits != 16 && t.bits != 32) value = "(" + value + " & " + mask(t) + ")";
    return "((" + ctype(t) + ")(" + value + "))";
}
std::string number(const Big& n) {
    if (n < 0) return "(-" + Big(-n - 1).str() + "L - 1L)";
    return n.str() + "UL";
}
Big numericConstant(Big raw,const Type& from,const Type& to) {
    if (from == to) return raw;
    if (from.kind == Type::Float || to.kind == Type::Float) {
        FloatParts value;
        if (from.kind == Type::Float) value=splitFloat(raw,from.bits);
        else { value.negative=raw<0;value.numerator=value.negative ? -raw : raw;value.denominator=Big(1)<<from.fraction; }
        if (to.kind == Type::Float) return joinFloat(value,to.bits);
        if (to.kind == Type::Boolean) return value.nan || value.infinity || value.numerator != 0 ? 1 : 0;
        if (value.nan || value.infinity) throw Error({},"non-finite constant has no target integer representation");
        raw=(value.numerator<<to.fraction)/value.denominator;
        return value.negative ? -raw : raw;
    }
    if (to.kind == Type::Boolean) return raw != 0 ? 1 : 0;
    if (to.fraction >= from.fraction) raw <<= to.fraction-from.fraction;
    else raw /= Big(1) << (from.fraction-to.fraction);
    return raw;
}
std::string scalarConversion(const std::string& expression,const Type& from,const Type& to,const Value* known = nullptr) {
    if (from == to) return expression;
    if (known && known->kind == Value::Number) return number(numericConstant(known->number,from,to));
    if (from.kind == Type::Float || to.kind == Type::Float) {
        if (from.kind != Type::Exact) (void)ctype(from);
        (void)ctype(to);
        auto kind = [](const Type& type) { return type.kind == Type::Float ? 2 : type.kind == Type::Boolean ? 3 : type.sign || type.kind == Type::Exact ? 1 : 0; };
        return "vx_float_convert((uint32_t)("+expression+"), "+std::to_string(from.kind == Type::Exact ? 32 : width(from))+", "+
            std::to_string(kind(from))+", "+std::to_string(from.fraction)+", "+std::to_string(width(to))+", "+
            std::to_string(kind(to))+", "+std::to_string(to.fraction)+")";
    }
    if (to.kind == Type::Boolean || (from.kind != Type::Fixed && to.kind != Type::Fixed) || from == to) return expression;
    return "vx_rescale((uint32_t)("+expression+"), "+std::to_string(from.kind == Type::Exact ? 32 : width(from))+
        ", "+(from.sign || from.kind == Type::Exact ? "1" : "0")+", "+std::to_string(from.fraction)+", "+std::to_string(to.fraction)+")";
}
std::string cName(const std::string& name) {
    if (name.empty()) throw Error({}, "empty backend symbol");
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            throw Error({}, "MSX export name is not a C identifier: " + name);
    return name;
}
std::string globalName(const Graph& graph,Id id) {
    const auto& definition=graph.nodes[id];const auto& source=graph.nodes[definition.site];
    return source.key=="external-binding" ? "vx_ext_"+cName(source.text) : "vxg_"+std::to_string(id);
}
void put(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out || !(out << contents) || !out.flush()) throw Error({}, "cannot write backend output: " + path.string());
}
std::string fingerprint(const std::string& bytes) {
    // Portable edit detection, not an authentication mechanism.
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ULL; }
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

struct Function {
    Id invocation;
    std::string name;
    std::vector<Id> parameters;
    std::vector<Id> environment, locations;
};

class Module {
public:
    const Graph& g;
    const Root& root;
    bool staticFrame;
    std::vector<Function> functions;
    std::map<Id, std::string> materialized;
    std::set<Id> neededSlots;
    std::set<Id> globalStorage;
    std::set<Id> recursiveFunctions;
    std::map<Id, std::string> tables;
    std::map<std::pair<Id, std::string>, std::string> arrayTables;
    std::map<std::string, std::string> externals;
    std::map<std::string, std::string> arrayTypes;
    std::ostringstream typeDefinitions, data, prototypes, bodies;
    std::size_t frameBytes = 0;
    std::size_t materializedCount = 0;
    Module(const Graph& graph, const Root& r, bool exclusive) : g(graph), root(r), staticFrame(exclusive) {
        for (const auto& node : g.nodes)
            if (node.rule == Rule::Load && node.fact && !node.fact->value.known()) neededSlots.insert(node.slot);
        for (Id id = 0; id < g.nodes.size(); ++id) {
            const auto& n = g.nodes[id];
            if (n.rule == Rule::Invoke && n.runtimeBackedge != none) {
                recursiveFunctions.insert(n.runtimeBackedge);
                for (Id scope = n.scope; scope != none; ) {
                    const auto& owner = g.nodes[g.scopes[scope].node];
                    if (owner.slot != none && owner.region != none) {
                        recursiveFunctions.insert(owner.region);
                        if (owner.region == n.runtimeBackedge) break;
                    }
                    scope = owner.slot != none ? g.scopes[scope].caller : g.scopes[scope].parent != none ? g.scopes[scope].parent : g.scopes[scope].caller;
                }
            }
            if (n.rule == Rule::Invoke && n.fact && n.body != none && g.nodes[n.body].noinline)
                for (Id capture : environment(id)) neededSlots.insert(capture);
        }
    }
    const Fact& fact(Id id) const {
        if (!g.nodes.at(id).fact) throw Error(g.nodes[id].span, "backend encountered an unresolved obligation");
        return *g.nodes[id].fact;
    }
    const Type& slotType(Id id) const {
        if (g.nodes[id].fact) return g.nodes[id].fact->type;
        if (g.nodes[id].inferredType) return *g.nodes[id].inferredType;
        throw Error(g.nodes[id].span,"captured storage has no resolved type");
    }
    std::vector<Id> environment(Id id) const {
        std::set<Id> captures;
        auto add = [&](Id slot) {
            if (g.nodes[slot].rule == Rule::Alias) slot = g.nodes[slot].slot;
            const auto& n = g.nodes[slot];
            if (n.rule==Rule::Global) return;
            if ((n.fact || n.inferredType) && slotType(slot).concrete()) captures.insert(slot);
        };
        for (Id scope = g.nodes[id].lexicalScope; scope != none; scope = g.scopes[scope].parent)
            for (const auto& [name,slot] : g.scopes[scope].slots) { (void)name; add(slot); }
        for (Id receiver : g.nodes[id].receivers) add(receiver);
        return {captures.begin(),captures.end()};
    }
    Type parameterType(Id invocation, std::size_t i) const {
        const auto& call=g.nodes[invocation];
        if(i>=call.branches.size()) throw Error(call.span,"external call has no resolved ABI parameter fact");
        return fact(call.branches[i]).type;
    }
    std::string typeName(const Type& t) {
        if (!aggregateType(t)) return ctype(t);
        if (auto it = arrayTypes.find(t.str()); it != arrayTypes.end()) return it->second;
        (void)storage(t);
        std::string name = "vxa_" + fingerprint(t.str());
        arrayTypes[t.str()] = name;
        if (t.kind == Type::Record) {
            std::string definition = "typedef struct {";
            for (std::size_t i = 0; i < t.fields.size(); ++i)
                definition += " " + typeName(t.fields[i]) + " f" + std::to_string(i) + ";";
            if (t.fields.empty()) definition += " uint8_t unused;";
            typeDefinitions << "#ifndef VX_TYPE_" << name << "\n#define VX_TYPE_" << name << "\n"
                            << definition << " } " << name << ";\n#endif\n";
            return name;
        }
        // Physical storage is flat; logical rank and shape stay in graph facts.
        std::string itemType = typeName(leaf(t));
        typeDefinitions << "#ifndef VX_TYPE_" << name << "\n#define VX_TYPE_" << name << "\n"
                        << "typedef struct { " << itemType << " items["
                        << std::max(elements(t), 1U) << "]; } " << name << ";\n#endif\n";
        return name;
    }
    std::string arrayConstant(Id id, const Type& target, const std::string& exportedName = {}) {
        auto key = std::make_pair(id, target.str());
        if (auto it = arrayTables.find(key); it != arrayTables.end()) return it->second;
        const auto& f = fact(id);
        std::string name = exportedName.empty() ? "vxt_" + std::to_string(tables.size() + arrayTables.size()) : exportedName;
        arrayTables[key] = name;
        std::function<std::string(const Value&, const Type&,const Type&)> initializer = [&](const Value& value, const Type& type,const Type& from) -> std::string {
            if (type.kind == Type::Record) {
                std::string out = "{";
                for (std::size_t i = 0; i < type.fields.size(); ++i) {
                    if (i) out += ",";
                    out += initializer(value.elements.at(i), type.fields[i],from.fields[i]);
                }
                if (type.fields.empty()) out += "0";
                return out + "}";
            }
            if (type.kind == Type::Array) {
                std::string out = "{{"; bool first = true;
                std::function<void(const Value&, const Type&,const Type&)> flatten = [&](const Value& item, const Type& itemType,const Type& sourceType) {
                    if (itemType.kind == Type::Array) {
                        for (const auto& child : item.elements) flatten(child, *itemType.element,*sourceType.element);
                    } else {
                        if (!first) out += ",";
                        first = false; out += initializer(item, itemType,sourceType);
                    }
                };
                flatten(value, type,from);
                if (first) out += "0";
                return out + "}}";
            }
            if (value.kind != Value::Number) throw Error(g.nodes[id].span, "constant aggregate contains a non-scalar leaf");
            Big raw=numericConstant(value.number,from,type);
            return "(" + ctype(type) + ")" + number(raw);
        };
        std::string physicalType = typeName(target);
        data << (exportedName.empty() ? "static const " : "const ") << physicalType << ' ' << name << " = " << initializer(f.value, target,f.type) << ";\n";
        return name;
    }
    std::string table(Id id) {
        if (auto it = tables.find(id); it != tables.end()) return it->second;
        const auto& f = fact(id);
        if (!f.value.known()) throw Error(g.nodes[id].span, "MSX backend currently requires constant arrays and strings");
        Type element = f.type.kind == Type::String ? Type::integer(8, false) : *f.type.element;
        (void)ctype(element);
        std::string name = "vxt_" + std::to_string(tables.size() + arrayTables.size());
        tables[id] = name;
        std::size_t size = f.type.kind == Type::String ? f.value.bytes.size() : f.type.size;
        if (size * storage(element) > 8192) throw Error(g.nodes[id].span, "constant table exceeds one NEO-8 segment; split the asset");
        data << "static const " << ctype(element) << ' ' << name << '[' << std::max<std::size_t>(size, 1) << "] = {";
        for (std::size_t i = 0; i < size; ++i) {
            if (i) data << ',';
            if (i % 16 == 0) data << '\n';
            Big n = f.type.kind == Type::String ? Big(static_cast<unsigned char>(f.value.bytes[i])) : f.value.elements[i].number;
            data << number(n);
        }
        if (!size) data << '0';
        data << "\n};\n";
        return name;
    }
    std::string materialize(Id id) {
        if (g.nodes[id].runtimeBackedge != none) id = g.nodes[id].runtimeBackedge;
        if (auto it = materialized.find(id); it != materialized.end()) return it->second;
        std::string name = "vxn_" + cName(root.name) + "_" + std::to_string(materializedCount++);
        std::vector<Id> params;
        for (Id arg : g.nodes[id].carried) if (!fact(arg).value.known()) params.push_back(arg);
        std::set<Id> locations;
        for (Id receiver : g.nodes[id].receivers)
            for (Id offset : g.nodes[receiver].inputs) if (!fact(offset).value.known()) locations.insert(offset);
        functions.push_back({id, name, params, environment(id), {locations.begin(),locations.end()}});
        materialized[id] = name;
        return name;
    }
    std::string prototype(const Function& f, bool names) {
        bool aggregateResult = aggregateType(fact(f.invocation).type);
        std::string result = (aggregateResult ? "void" : typeName(fact(f.invocation).type)) + " " + f.name + "(";
        bool first = true;
        auto parameter = [&](const std::string& type,const std::string& name) {
            if (!first) result += ", ";
            first = false;
            result += type; if (names) result += " " + name;
        };
        for (std::size_t i = 0; i < f.parameters.size(); ++i) {
            const Type& t = fact(f.parameters[i]).type;
            parameter((aggregateType(t) ? "const " : "") + typeName(t) + (aggregateType(t) ? "*" : ""),"p"+std::to_string(i));
        }
        for (std::size_t i = 0; i < f.environment.size(); ++i) parameter(typeName(slotType(f.environment[i]))+"*","e"+std::to_string(i));
        for (std::size_t i = 0; i < f.locations.size(); ++i) parameter(typeName(fact(f.locations[i]).type),"o"+std::to_string(i));
        if (aggregateResult) parameter(typeName(fact(f.invocation).type)+"*","vx_result");
        if (first) result += "void";
        return result + ") VX_ABI";
    }
    std::string generate();
};

// This is a projection of established graph facts. It never resolves a source
// name, selects an overload, infers a type, or executes a Vexel expression.
class Body {
    Module& m;
    Function function;
    std::map<Id, std::string> values;
    std::map<Id, std::string> environment;
    std::set<Id> states;
    std::map<std::string, Type> declarations;
    struct ExitTarget { std::string label, output; Type type; };
    std::map<Id,ExitTarget> returns;
    std::map<Id,std::pair<std::string,std::string>> loops;
    std::ostringstream code;
    unsigned indent = 1;
    unsigned nextTemporary = 0;
    const Node& node(Id id) const { return m.g.nodes.at(id); }
    const Fact& fact(Id id) const { return m.fact(id); }
    void line(const std::string& text) { code << std::string(indent * 4, ' ') << text << '\n'; }
    std::string variable(const std::string& prefix, Id id, const Type& t) {
        if (prefix=="s" && node(id).rule==Rule::Global) {
            m.globalStorage.insert(id);return globalName(m.g,id);
        }
        if (prefix == "s") if (auto found = environment.find(id); found != environment.end()) return found->second;
        (void)m.typeName(t);
        std::string name = prefix + std::to_string(id);
        declarations.emplace(name, t);
        return name;
    }
    std::string temporary(const Type& t) {
        std::string name = "c" + std::to_string(nextTemporary++);
        declarations.emplace(name, t); return name;
    }
    std::string aggregate(Id id, const Type* target = nullptr) {
        std::string result = value(id);
        if (target && !(*target == fact(id).type)) {
            if (fact(id).value.known()) return m.arrayConstant(id,*target);
            if (result.empty()) throw Error(node(id).span,"missing runtime aggregate conversion source");
            std::string converted = temporary(*target);
            copy(converted,result,*target,"0","0",&fact(id).type); return converted;
        }
        if (!result.empty()) return result;
        if (fact(id).value.known()) return m.arrayConstant(id, target ? *target : fact(id).type);
        throw Error(node(id).span, "missing runtime array realization");
    }
    void copy(const std::string& destination, const std::string& source, const Type& t,
              const std::string& destinationOffset = "0", const std::string& sourceOffset = "0",const Type* from = nullptr,const Value* known = nullptr) {
        if (t.kind == Type::Record) {
            for (std::size_t i = 0; i < t.fields.size(); ++i)
                copy(destination + ".f" + std::to_string(i), source + ".f" + std::to_string(i), t.fields[i],"0","0",from ? &from->fields[i] : nullptr);
            return;
        }
        if (t.kind != Type::Array) { line(destination + " = " + normalize(from ? scalarConversion(source,*from,t,known) : source, t) + ";"); return; }
        if (!elements(t)) return;
        std::string i = temporary(Type::integer(16, false));
        line("for (" + i + " = 0; " + i + " < " + std::to_string(elements(t)) + "U; ++" + i + ") {");
        ++indent;
        copy(destination + ".items[" + destinationOffset + " + " + i + "]",
             source + ".items[" + sourceOffset + " + " + i + "]", leaf(t),"0","0",from ? &leaf(*from) : nullptr);
        --indent; line("}");
    }
    Type comparisonStorage(const Type& a,const Type& b) {
        if (a.kind == Type::Exact) return b;
        if (a.kind == Type::Array) return Type::array(comparisonStorage(*a.element,*b.element),a.size);
        if (a.kind == Type::Record) {
            Type result = a;
            for (std::size_t i=0;i<a.fields.size();++i) result.fields[i]=comparisonStorage(a.fields[i],b.fields[i]);
            return result;
        }
        return a;
    }
    void compare(const std::string& order,const std::string& a,const Type& at,const std::string& b,const Type& bt) {
        if (at.kind == Type::Record) {
            for (std::size_t i=0;i<at.fields.size();++i)
                compare(order,a+".f"+std::to_string(i),at.fields[i],b+".f"+std::to_string(i),bt.fields[i]);
            return;
        }
        if (at.kind == Type::Array) {
            unsigned count = std::min(elements(at),elements(bt));
            if (count) {
                std::string index = temporary(Type::integer(16,false));
                line("for ("+index+" = 0; "+order+" == 0 && "+index+" < "+std::to_string(count)+"U; ++"+index+") {");
                ++indent; compare(order,a+".items["+index+"]",leaf(at),b+".items["+index+"]",leaf(bt)); --indent; line("}");
            }
            if (at.size != bt.size) line("if ("+order+" == 0) "+order+" = "+(at.size<bt.size ? "-1" : "1")+";");
            return;
        }
        std::string cast = at.sign || bt.sign ? "int32_t" : "uint32_t";
        if (at.kind == Type::Float || bt.kind == Type::Float) {
            Type target = Type::floating(std::max(at.bits,bt.bits));
            std::string left=scalarConversion(a,at,target), right=scalarConversion(b,bt,target);
            line("if ("+order+" == 0) "+order+" = (int8_t)vx_float_binary((uint32_t)("+left+"), (uint32_t)("+right+"), "+std::to_string(target.bits)+", 11);");
            return;
        }
        std::string left = "(("+cast+")("+a+"))", right = "(("+cast+")("+b+"))";
        line("if ("+order+" == 0) "+order+" = ("+left+" > "+right+") - ("+left+" < "+right+");");
    }
    void transferBytes(const std::string& bytes,const std::string& object,const Type& type,const std::string& offset,bool decode) {
        if (type.kind == Type::Record) {
            std::size_t position=0;
            for (std::size_t i=0;i<type.fields.size();++i) {
                transferBytes(bytes,object+".f"+std::to_string(i),type.fields[i],offset+" + "+std::to_string(position),decode);
                position+=*representationBytes(type.fields[i]);
            }
            return;
        }
        if (type.kind == Type::Array) {
            if (!elements(type)) return;
            const Type& item=leaf(type);
            std::string index=temporary(Type::integer(16,false));
            line("for ("+index+" = 0; "+index+" < "+std::to_string(elements(type))+"U; ++"+index+") {");
            ++indent;
            transferBytes(bytes,object+".items["+index+"]",item,offset+" + "+index+" * "+std::to_string(*representationBytes(item))+"U",decode);
            --indent;line("}");return;
        }
        (void)ctype(type);
        std::string value;
        for (std::size_t i=0;i<*representationBytes(type);++i) {
            std::string byte=bytes+".items["+offset+" + "+std::to_string(i)+"]";
            if (decode) {
                if (i) value+=" | ";
                value+="((uint32_t)"+byte+" << "+std::to_string(i*8)+")";
            } else line(byte+" = (uint8_t)(((uint32_t)("+object+") & "+mask(type)+") >> "+std::to_string(i*8)+");");
        }
        if (decode) {
            if (type.kind==Type::Boolean) line("if (("+value+") > 1) vx_trap(5);");
            copy(object,"("+value+")",type);
        }
    }
    std::string representation(Id id) {
        const auto& n=node(id); const Type& from=fact(n.inputs[0]).type; const Type& to=fact(id).type;
        std::string result=variable("v",id,to);
        bool sourceBits=from.kind==Type::Array && from.element->kind==Type::Boolean;
        bool targetBits=to.kind==Type::Array && to.element->kind==Type::Boolean;
        if ((sourceBits && to.kind==Type::Integer) || (from.kind==Type::Integer && targetBits)) {
            std::size_t count=sourceBits ? from.size : to.size;
            (void)ctype(sourceBits ? to : from);
            std::string input=sourceBits ? aggregate(n.inputs[0]) : value(n.inputs[0]);
            std::string index=temporary(Type::integer(8,false));
            if (sourceBits) line(result+" = 0;");
            line("for ("+index+" = 0; "+index+" < "+std::to_string(count)+"U; ++"+index+") {");++indent;
            if (sourceBits) line(result+" |= (uint32_t)"+input+".items["+index+"] << "+index+";");
            else line(result+".items["+index+"] = (uint8_t)(((uint32_t)("+input+") >> "+index+") & 1U);");
            --indent;line("}");return result;
        }
        bool decode=from.kind==Type::Array && *from.element==Type::integer(8,false);
        std::string input=aggregateType(from) ? aggregate(n.inputs[0]) : value(n.inputs[0]);
        if (decode) transferBytes(input,result,to,"0",true);
        else transferBytes(result,input,from,"0",false);
        return result;
    }
    std::string offset(const std::string& index, const Type& element) {
        if (elements(element) == 1) return "(uint16_t)(" + index + ")";
        std::string result = temporary(Type::integer(16, false));
        line(result + " = (uint16_t)vx_mul((uint32_t)(" + index + "), " + std::to_string(elements(element)) + "UL);");
        return result;
    }
    std::string assign(Id id, const std::string& expression) {
        const auto& t = fact(id).type;
        if (t.kind == Type::Empty) { if (!expression.empty()) line(expression + ";"); return {}; }
        std::string name = variable("v", id, t);
        copy(name, expression, t);
        return name;
    }
    std::string control(Id id) {
        const auto& n = node(id);
        state(n.previous);
        std::string condition = value(n.inputs[0]);
        const auto& f = fact(id);
        bool known = fact(n.inputs[0]).value.known();
        if (known) {
            if (n.branches.empty()) return number(f.value.number);
            state(n.ends[0]);
            std::string v = value(n.branches[0]);
            if (f.type.kind == Type::Empty) return {};
            return f.value.known() ? (aggregateType(f.type) ? "" : number(f.value.number)) : assign(id, v);
        }
        std::string output;
        if (f.type.kind != Type::Empty && !f.value.known()) output = variable("v", id, f.type);
        const auto savedValues = values;
        const auto savedStates = states;
        bool shortCircuit = n.rule == Rule::ShortCircuit;
        for (unsigned arm = 0; arm != 2; ++arm) {
            line(arm == 0 ? "if (" + condition + ") {" : "} else {");
            ++indent;
            values = savedValues; states = savedStates;
            std::string v;
            if (!shortCircuit) { state(n.ends[arm]); v = value(n.branches[arm]); }
            else if ((arm == 0) == (n.text == "&&")) { state(n.ends[0]); v = value(n.branches[0]); }
            else v = arm == 0 ? "1" : "0";
            bool completes = shortCircuit ? ((arm == 0) != (n.text == "&&") || node(n.ends[0]).completion.normal) : node(n.ends[arm]).completion.normal;
            if (!output.empty() && completes) {
                if (aggregateType(f.type)) v = aggregate(n.branches[arm],&f.type);
                else if (!shortCircuit) v = scalarConversion(v,fact(n.branches[arm]).type,f.type,&fact(n.branches[arm]).value);
                copy(output, v, f.type);
            }
            --indent;
        }
        line("}");
        values = savedValues; states = savedStates;
        if (f.type.kind == Type::Empty) return {};
        return output.empty() ? (aggregateType(f.type) ? "" : number(f.value.number)) : output;
    }
public:
    Body(Module& module, Function f) : m(module), function(std::move(f)) {
        for (std::size_t i = 0; i < function.parameters.size(); ++i)
            values[function.parameters[i]] = aggregateType(fact(function.parameters[i]).type) ? "(*p"+std::to_string(i)+")" : "p" + std::to_string(i);
        for (std::size_t i = 0; i < function.environment.size(); ++i) environment[function.environment[i]] = "(*e"+std::to_string(i)+")";
        for (std::size_t i = 0; i < function.locations.size(); ++i) values[function.locations[i]] = "o"+std::to_string(i);
    }
    void state(Id id) {
        if (id == none || states.contains(id)) return;
        const auto& n = node(id);
        switch (n.rule) {
        case Rule::Entry: case Rule::LoopHeader: break;
        case Rule::CallState: (void)value(n.region); break;
        case Rule::Step: state(n.inputs[0]); if (node(n.inputs[0]).completion.normal) (void)value(n.inputs[1]); break;
        case Rule::Store: {
            state(n.inputs[0]);
            if (!node(n.inputs[0]).completion.normal) { values[id] = {}; break; }
            std::string v = value(n.inputs[1]);
            const auto& t = fact(n.inputs[1]).type;
            bool persistent=node(n.slot).rule==Rule::Global;
            if (persistent || m.neededSlots.contains(n.slot)) {
                std::string s = variable("s", n.slot, t);
                if (persistent && node(n.slot).expansion!=none) line("vxg_ensure_"+std::to_string(n.slot)+"();");
                if (aggregateType(t) && v.empty()) v = aggregate(n.inputs[1]);
                copy(s, v, t);
                if (persistent && !node(n.slot).external) line("vxg_state_"+std::to_string(n.slot)+" = 2;");
            }
            values[id] = v;
            break;
        }
        case Rule::StateOf: (void)value(n.inputs[0]); break;
        case Rule::Resume: state(n.inputs[0]); break;
        case Rule::Exit: {
            state(n.inputs[0]);
            if (!node(n.inputs[0]).completion.normal) break;
            if (n.text == "return") {
                const auto& target = returns.at(n.region);
                std::string result = value(n.inputs[1]);
                if (!target.output.empty()) {
                    if (aggregateType(target.type) && result.empty()) result = aggregate(n.inputs[1]);
                    copy(target.output,result,target.type,"0","0",&fact(n.inputs[1]).type,&fact(n.inputs[1]).value);
                }
                line("goto " + target.label + ";");
            } else {
                const auto& target = loops.at(n.region);
                line("goto " + (n.text == "break" ? target.first : target.second) + ";");
            }
            break;
        }
        default: throw Error(n.span, "unsupported backend state edge: " + ruleName(n.rule));
        }
        states.insert(id);
    }
    std::string value(Id id) {
        if (auto it = values.find(id); it != values.end()) return it->second;
        const auto& n = node(id);
        const auto& f = fact(id);
        if (n.previous != none && node(n.previous).fact && !node(n.previous).completion.normal && n.rule != Rule::Entry) {
            state(n.previous); values[id] = {}; return {};
        }
        std::string result;
        auto literal = [&]() -> std::string {
            if (f.type.kind == Type::Empty) return {};
            if (f.value.kind == Value::Number) return number(f.value.number);
            // Composite constants acquire physical storage only when accessed
            // at runtime. Compile-time ranges/lengths need no target table.
            if (aggregateType(f.type) || f.type.kind == Type::String) return {};
            throw Error(n.span, "unsupported value in MSX projection: " + f.type.str());
        };
        switch (n.rule) {
        case Rule::CustomIteration: {
            std::string end="vx_custom_break_"+std::to_string(id);
            loops[id]={end,""};state(n.inputs[0]);line(end+":;");loops.erase(id);
            break;
        }
        case Rule::IterationBody: {
            auto& region=loops.at(n.region);std::string previous=region.second;
            std::string next="vx_custom_continue_"+std::to_string(id);
            region.second=next;state(n.inputs[0]);line(next+":;");loops.at(n.region).second=previous;
            break;
        }
        case Rule::Sequence: case Rule::Evaluate:
            state(n.endState);
            if (n.completion.normal && n.expansion != none) result = value(n.expansion);
            break;
        case Rule::Exit: state(id); break;
        case Rule::FunctionResult: {
            std::string label = "vx_return_" + std::to_string(id);
            std::string output;
            if (f.type.kind != Type::Empty && !f.value.known()) output = variable("v",id,f.type);
            returns[n.region] = {label,output,f.type};
            state(n.inputs[1]);
            if (node(n.inputs[1]).completion.normal) {
                std::string tail = value(n.inputs[0]);
                if (!output.empty()) {
                    if (aggregateType(f.type) && tail.empty()) tail = aggregate(n.inputs[0]);
                    copy(output,tail,f.type);
                }
            }
            line(label + ":;"); returns.erase(n.region);
            result = output.empty() ? literal() : output;
            break;
        }
        case Rule::Constant: case Rule::Global: case Rule::Resource: case Rule::Solve:
        case Rule::FieldIndex: case Rule::Probe: case Rule::CallProbe: result = literal(); break;
        case Rule::Sort: (void)value(n.inputs[0]);result=literal();break;
        case Rule::Slot: case Rule::Type: case Rule::TypeJoin: break;
        case Rule::ShapeCheck: {
            std::string check=value(n.expansion);
            if(!fact(n.expansion).value.known()) line("if (!("+check+")) vx_trap(9);");
            break;
        }
        case Rule::Input:
            if(n.expansion!=none) {result=value(n.expansion);break;}
            // Parameter facts deliberately shed the argument's effects. A
            // reused pure invocation may refer to another call site's argument;
            // its effects belong to that caller, never to this parameter load.
            if (f.value.known()) { result = literal(); break; }
            if (n.inputs.empty()) throw Error(n.span, "unbound runtime input in C projection");
            result = aggregateType(f.type) ? aggregate(n.inputs[0],&f.type) : scalarConversion(value(n.inputs[0]),fact(n.inputs[0]).type,f.type,&fact(n.inputs[0]).value);
            result = assign(id, result);
            break;
        case Rule::PropertyValue:
            // A projected parameter still denotes that invocation's input.
            // Its expansion can reach the caller's producer for compile-time
            // reasoning, but a materialized body must use its own ABI binding.
            if(auto supplied=values.find(n.inputs[0]);supplied!=values.end()) {
                result=supplied->second;break;
            }
            result=n.expansion==none ? literal() : value(n.expansion);break;
        case Rule::PropertyLoad:
            result=n.expansion==none ? literal() : value(n.expansion);break;
        case Rule::PropertyCall:
            result=value(n.signature);break;
        case Rule::Bind: case Rule::Unpack: result = value(n.inputs[0]); break;
        case Rule::Load:
            state(n.inputs[0]);
            result = f.value.known() ? literal() : assign(id, variable("s", n.slot, f.type));
            break;
        case Rule::GlobalRead: {
            state(n.inputs[0]);
            if (f.value.known()) {result=literal();break;}
            std::string storage=variable("s",n.slot,f.type);
            if (!node(n.slot).external) {
                if (node(n.slot).expansion!=none) line("vxg_ensure_"+std::to_string(n.slot)+"();");
                else line("if (vxg_state_"+std::to_string(n.slot)+" != 2) vx_trap(7);");
            }
            result=assign(id,storage);break;
        }
        case Rule::Store: state(id); return values.at(id);
        case Rule::Finish:
            state(n.inputs[1]); if (node(n.inputs[1]).completion.normal) result = value(n.inputs[0]); break;
        case Rule::Adapt: case Rule::Cast:
            if(n.expansion!=none) {result=value(n.expansion);break;}
            result = value(n.inputs[0]);
            if (f.value.known()) result = literal();
            else if (n.rule == Rule::Cast && representationCast(fact(n.inputs[0]).type,f.type)) result = representation(id);
            else result = assign(id, aggregateType(f.type) ? aggregate(n.inputs[0], &f.type) : scalarConversion(result,fact(n.inputs[0]).type,f.type,&fact(n.inputs[0]).value));
            break;
        case Rule::Math: {
            state(n.previous);
            std::vector<std::string> arguments;for(Id input:n.inputs) arguments.push_back(value(input));
            if(n.expansion!=none) {result=value(n.expansion);break;}
            if(f.value.known()) {result=literal();break;}
            unsigned operation=mathOperation(n.text);
            Type operand=f.type.kind==Type::Boolean ? fact(n.inputs[0]).type : f.type;
            (void)ctype(operand);
            std::string a=scalarConversion(arguments[0],fact(n.inputs[0]).type,operand,&fact(n.inputs[0]).value),b="0";
            if(arguments.size()==2) b=scalarConversion(arguments[1],fact(n.inputs[1]).type,operation==16 ? Type::integer(32,false) : operand,&fact(n.inputs[1]).value);
            if(operation==16) {
                std::string base=temporary(operand),power=temporary(Type::integer(32,false)),answer=variable("v",id,operand);
                line(base+" = "+normalize(a,operand)+";");line(power+" = "+b+";");
                line(answer+" = "+number(numericConstant(1,Type::simple(Type::Exact),operand))+";");
                auto multiply=[&](const std::string& x,const std::string& y) {
                    if(operand.kind==Type::Float) return "vx_float_binary((uint32_t)("+x+"), (uint32_t)("+y+"), "+std::to_string(operand.bits)+", 2)";
                    if(operand.kind==Type::Fixed) return "vx_fixed_mul((uint32_t)("+x+"), (uint32_t)("+y+"), "+std::to_string(operand.bits)+", "+(operand.sign ? "1" : "0")+", "+std::to_string(operand.fraction)+")";
                    return "vx_mul((uint32_t)("+x+"), (uint32_t)("+y+"))";
                };
                line("while ("+power+") {");++indent;
                line("if ("+power+" & 1) "+answer+" = "+normalize(multiply(answer,base),operand)+";");
                line(power+" >>= 1;");line("if ("+power+") "+base+" = "+normalize(multiply(base,base),operand)+";");
                --indent;line("}");result=answer;break;
            }
            if(operation==14) result=operand.kind==Type::Float ? "((uint32_t)("+a+") & "+number((Big(1)<<(operand.bits-1))-1)+")" :
                operand.sign ? "("+a+" < 0 ? 0UL - (uint32_t)("+a+") : (uint32_t)("+a+"))" : a;
            else if(operation==15) result=operand.kind==Type::Float ?
                "vx_float_binary((uint32_t)("+a+"), (uint32_t)("+b+"), "+std::to_string(operand.bits)+", 4)" :
                "vx_divmod((uint32_t)("+a+"), (uint32_t)("+b+"), "+std::to_string(operand.bits)+", "+(operand.sign ? "1" : "0")+", 1)";
            else if((operation==5 || operation==6) && operand.kind!=Type::Float)
                result="("+a+(operation==5 ? " < " : " >= ")+b+" ? "+a+" : "+b+")";
            else if(operation!=12 && operand.kind!=Type::Float && operand.kind!=Type::Fixed) result=a;
            else {
                if(operation==12) b=operand.sign ? "1" : "0";
                else if(operand.kind==Type::Fixed) {operation+=128;b=std::to_string(operand.fraction+(operand.sign ? 256 : 0));}
                result="vx_math((uint32_t)("+a+"), (uint32_t)("+b+"), "+std::to_string(operand.bits)+", "+std::to_string(operation)+")";
            }
            result=assign(id,result);break;
        }
        case Rule::Unary: {
            state(n.previous);
            std::string a = value(n.inputs[0]);
            if (f.value.known()) { result = literal(); break; }
            if (f.type.kind == Type::Float) {
                result = n.text == "+" ? a : "((uint32_t)("+a+") "+(n.text == "-" ? "^ " : "& ")+
                    number(n.text == "-" ? Big(1)<<(f.type.bits-1) : (Big(1)<<(f.type.bits-1))-1)+")";
                result=assign(id,result); break;
            }
            if (n.text == "-") result = "(0UL - (uint32_t)(" + a + "))";
            else if (n.text == "+") result = a;
            else if (n.text == "~") result = "~(uint32_t)(" + a + ")";
            else if (n.text == "!") result = "!(" + a + ")";
            else if (n.text == "|") result = f.type.sign ? "(" + a + " < 0 ? 0UL - (uint32_t)(" + a + ") : (uint32_t)(" + a + "))" : a;
            else throw Error(n.span, "unsupported residual unary operation");
            result = assign(id, result); break;
        }
        case Rule::Binary: {
            state(n.previous);
            if (n.expansion != none) { result = value(n.expansion); break; }
            std::string a = value(n.inputs[0]);
            std::string b = value(n.inputs[1]);
            if (f.value.known()) { result = literal(); break; }
            Type t = f.type;
            const auto& op = n.text;
            bool cmp = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
            if (cmp) {
                t = fact(n.inputs[0]).type;
                const auto& bt = fact(n.inputs[1]).type;
                if (aggregateType(t)) {
                    Type left = comparisonStorage(t,bt), right = comparisonStorage(bt,t);
                    std::string order = temporary(Type::integer(8,true)); line(order+" = 0;");
                    compare(order,aggregate(n.inputs[0],&left),left,aggregate(n.inputs[1],&right),right);
                    result = assign(id,"("+order+" == 2 ? "+(op == "!=" ? "1" : "0")+" : "+order+" "+op+" 0)"); break;
                }
                if (t.kind == Type::Exact) t = bt;
                if (!scalar(t) || (bt.kind != Type::Exact && !scalar(bt)))
                    throw Error(n.span, "runtime composite comparisons are not implemented by MSX backend");
                if (t.kind == Type::Float && bt.kind == Type::Float) t=Type::floating(std::max(t.bits,bt.bits));
                a = scalarConversion(a,fact(n.inputs[0]).type,t,&fact(n.inputs[0]).value); b = scalarConversion(b,bt,t,&fact(n.inputs[1]).value);
                if (t.kind == Type::Float) {
                    unsigned operation = op == "==" ? 5 : op == "!=" ? 6 : op == "<" ? 7 : op == "<=" ? 8 : op == ">" ? 9 : 10;
                    result=assign(id,"vx_float_binary((uint32_t)("+a+"), (uint32_t)("+b+"), "+std::to_string(t.bits)+", "+std::to_string(operation)+")"); break;
                }
                std::string c = t.sign ? "int32_t" : "uint32_t";
                result = "((" + c + ")(" + a + ") " + op + " (" + c + ")(" + b + "))";
            } else {
            a = scalarConversion(a,fact(n.inputs[0]).type,t,&fact(n.inputs[0]).value);
            if (op != "<<" && op != ">>") b = scalarConversion(b,fact(n.inputs[1]).type,t,&fact(n.inputs[1]).value);
            if (t.kind == Type::Float) {
                unsigned operation = op == "+" ? 0 : op == "-" ? 1 : op == "*" ? 2 : op == "/" ? 3 : 4;
                result="vx_float_binary((uint32_t)("+a+"), (uint32_t)("+b+"), "+std::to_string(t.bits)+", "+std::to_string(operation)+")";
            }
            else if ((op == "*" || op == "/") && t.kind == Type::Fixed)
                result = std::string(op == "*" ? "vx_fixed_mul" : "vx_fixed_div")+"((uint32_t)("+a+"), (uint32_t)("+b+"), "+
                    std::to_string(t.bits)+", "+(t.sign ? "1" : "0")+", "+std::to_string(t.fraction)+")";
            else if (op == "*") result = "vx_mul((uint32_t)(" + a + "), (uint32_t)(" + b + "))";
            else if (op == "/" || op == "%")
                result = "vx_divmod((uint32_t)(" + a + "), (uint32_t)(" + b + "), " + std::to_string(width(t)) + ", " + (t.sign ? "1" : "0") + ", " + (op == "%" ? "1" : "0") + ")";
            else if (op == "<<" || op == ">>")
                result = "vx_shift((uint32_t)(" + a + "), (uint32_t)(" + b + "), " + std::to_string(width(t)) + ", " + (op == ">>" ? "1" : "0") + ", " + (t.sign ? "1" : "0") + ")";
            else result = "((uint32_t)(" + a + ") " + op + " (uint32_t)(" + b + "))";
            }
            result = assign(id, result); break;
        }
        case Rule::Array: {
            if(n.expansion!=none) {result=value(n.expansion);break;}
            std::vector<std::string> items;
            for (Id input : n.inputs) items.push_back(value(input));
            if (f.value.known()) { result = literal(); break; }
            result = variable("v", id, f.type);
            for (std::size_t i = 0; i < n.inputs.size(); ++i) {
                std::string at = std::to_string(i * elements(*f.type.element));
                if (f.type.element->kind == Type::Array)
                    copy(result, aggregate(n.inputs[i], f.type.element.get()), *f.type.element, at);
                else if (f.type.element->kind == Type::Record)
                    copy(result + ".items[" + at + "]", aggregate(n.inputs[i], f.type.element.get()), *f.type.element);
                else copy(result + ".items[" + at + "]", items[i], *f.type.element,"0","0",&fact(n.inputs[i]).type,&fact(n.inputs[i]).value);
            }
            break;
        }
        case Rule::Construct: case Rule::Tuple: {
            for(Id input:n.inputs) (void)value(input);
            const auto& fields=n.environment.empty() ? n.inputs : n.environment;
            for(Id field:fields) (void)value(field);
            for(Id requirement:n.branches) if(requirement!=none)
                for(Id check:node(requirement).ends) (void)value(check);
            if (f.value.known()) { result = literal(); break; }
            result = variable("v", id, f.type);
            for (std::size_t i = 0; i < n.inputs.size(); ++i) {
                const auto& t = f.type.fields[i];
                copy(result + ".f" + std::to_string(i), aggregateType(t) ? aggregate(fields[i], &t) : value(fields[i]), t,
                    "0","0",aggregateType(t) ? nullptr : &fact(fields[i]).type,&fact(fields[i]).value);
            }
            break;
        }
        case Rule::Index: {
            state(n.previous);
            (void)value(n.inputs[0]);
            std::string b = value(n.inputs[1]);
            if (f.value.known()) { result = literal(); break; }
            const auto& source = fact(n.inputs[0]);
            if (source.type.kind == Type::Record) {
                std::string field = ".f" + fact(n.inputs[1]).value.number.str();
                result = assign(id, aggregate(n.inputs[0]) + field); break;
            }
            std::size_t size = source.type.kind == Type::String ? source.value.bytes.size() : source.type.size;
            line("if ((uint32_t)(" + b + ") >= " + std::to_string(size) + "UL) vx_trap(3);");
            if (source.type.kind == Type::String) result = assign(id, m.table(n.inputs[0]) + "[(uint16_t)(" + b + ")]");
            else {
                std::string a = aggregate(n.inputs[0]), at = offset(b, f.type);
                if (f.type.kind == Type::Array) {
                    result = variable("v", id, f.type);
                    copy(result, a, f.type, "0", at);
                } else result = assign(id, a + ".items[" + at + "]");
            }
            break;
        }
        case Rule::Update: {
            if(n.expansion!=none) {result=value(n.expansion);break;}
            (void)value(n.inputs[0]);
            std::string b = value(n.inputs[1]), item = value(n.inputs[2]);
            if (f.value.known()) { result = literal(); break; }
            std::string a = aggregate(n.inputs[0]);
            if (f.type.kind == Type::Record) {
                result = assign(id, a);
                std::size_t field = fact(n.inputs[1]).value.number.convert_to<std::size_t>();
                const auto& t = f.type.fields[field];
                copy(result + ".f" + std::to_string(field), aggregateType(t) ? aggregate(n.inputs[2], &t) : item, t,
                    "0","0",aggregateType(t) ? nullptr : &fact(n.inputs[2]).type,&fact(n.inputs[2]).value);
                break;
            }
            line("if ((uint32_t)(" + b + ") >= " + std::to_string(f.type.size) + "UL) vx_trap(3);");
            result = assign(id, a);
            std::string at = offset(b, *f.type.element);
            if (f.type.element->kind == Type::Array) copy(result, aggregate(n.inputs[2]), *f.type.element, at);
            else copy(result + ".items[" + at + "]", f.type.element->kind == Type::Record ? aggregate(n.inputs[2], f.type.element.get()) : item, *f.type.element,
                "0","0",aggregateType(*f.type.element) ? nullptr : &fact(n.inputs[2]).type,&fact(n.inputs[2]).value);
            break;
        }
        case Rule::Select: result = control(id); break;
        case Rule::ShortCircuit:
            result = n.key=="overloaded-logical" ? value(n.expansion) : control(id);
            break;
        case Rule::Iterate: case Rule::Repeat: {
            if (n.ends.empty()) { state(n.endState); break; }
            state(n.previous);
            std::string exit = "vx_break_" + std::to_string(id);
            for (std::size_t i = 0; i < n.ends.size(); ++i) {
                std::string next = "vx_continue_" + std::to_string(id) + "_" + std::to_string(i);
                if (n.rule == Rule::Repeat && i % 2 == 0 && i+1 < n.ends.size())
                    next = "vx_continue_" + std::to_string(id) + "_" + std::to_string(i+1);
                loops[id] = {exit,next};
                state(n.ends[i]);
                if (n.rule == Rule::Iterate || i % 2 != 0 || i+1 == n.ends.size()) line(next+":;");
            }
            line(exit+":;"); loops.erase(id); states.insert(n.endState);
            break;
        }
        case Rule::Loop: {
            state(n.previous);
            if (n.region == none) { state(n.endState); break; }
            std::string array, cursor;
            if (n.text != "condition") {
                array = aggregate(n.inputs[0]);
                cursor = temporary(Type::integer(16, false));
            }
            const auto savedValues = values;
            const auto savedStates = states;
            std::string exit = "vx_break_" + std::to_string(id), next = "vx_continue_" + std::to_string(id);
            loops[id] = {exit,next};
            states.insert(n.region);
            if (n.text == "condition") {
                line("for (;;) {"); ++indent;
                state(n.ends[0]);
                std::string guard = value(n.branches[0]);
                line("if (!(" + guard + ")) break;");
            } else {
                line("for (" + cursor + " = 0; " + cursor + " < " +
                     std::to_string(fact(n.inputs[0]).type.size) + "U; ++" + cursor + ") {");
                ++indent;
                const auto& t = fact(n.annotation).type;
                std::string at = offset(cursor, t);
                if (t.kind == Type::Array) {
                    std::string item = variable("v", n.annotation, t);
                    copy(item, array, t, "0", at); values[n.annotation] = item;
                } else values[n.annotation] = assign(n.annotation, array + ".items[" + at + "]");
            }
            state(n.ends.back()); (void)value(n.branches.back());
            line(next+":;");
            --indent; line("}");
            line(exit+":;"); loops.erase(id);
            values = savedValues; states = savedStates;
            break;
        }
        case Rule::Invoke: {
            state(n.previous);
            std::vector<std::string> args;
            for (Id arg : n.inputs) args.push_back(node(arg).rule == Rule::Deferred ? std::string{} : value(arg));
            const auto& fn = node(n.body);
            if (fn.external) {
                std::string name = fn.text == "msx_putchar" ? "vx_msx_putchar" : "vx_ext_" + cName(fn.text);
                bool aggregateResult=aggregateType(f.type);
                std::string proto = (aggregateResult ? "void" : ctype(f.type)) + " " + name + "(";
                std::string call = name + "(";
                bool first=true;
                auto argument=[&](const std::string& type,const std::string& value) {
                    if (!first) {proto+=", ";call+=", ";}first=false;proto+=type;call+=value;
                };
                for (std::size_t i = 0; i < args.size(); ++i) {
                    Type t = m.parameterType(id, i);
                    if (t.kind==Type::String) {
                        const auto& bytes=fact(n.inputs[i]).value.bytes;
                        if (bytes.size()>65535) throw Error(n.span,"external string argument exceeds the 16-bit length interface");
                        argument("const uint8_t*",m.table(n.inputs[i]));argument("uint16_t",std::to_string(bytes.size())+"U");
                    } else if (aggregateType(t)) argument("const "+m.typeName(t)+"*","&("+aggregate(n.inputs[i],&t)+")");
                    else argument(ctype(t),normalize(scalarConversion(args[i],fact(n.inputs[i]).type,t,&fact(n.inputs[i]).value),t));
                }
                if (aggregateResult) {
                    result=variable("v",id,f.type);argument(m.typeName(f.type)+"*","&"+result);
                }
                if (first) proto += "void";
                proto += ") VX_ABI VX_RESIDENT;";
                if (fn.text == "msx_putchar" && (args.size() != 1 || m.parameterType(id, 0) != Type::integer(8, false) || f.type.kind != Type::Empty))
                    throw Error(fn.span, "msx_putchar binding requires &!msx_putchar(c:#u8);");
                if (auto p = m.externals.find(name); p != m.externals.end() && p->second != proto)
                    throw Error(fn.span, "inconsistent external C signature");
                m.externals[name] = proto;
                if (aggregateResult) line(call+");");
                else result = assign(id, call + ")");
            } else if ((fn.noinline || n.runtimeBackedge != none) && (!f.value.known() || f.effect >= Effect::Control)) {
                std::string call = m.materialize(id) + "(";
                bool first = true;
                auto argument = [&](const std::string& expression) { if (!first) call += ", "; first = false; call += expression; };
                for (std::size_t i = 0; i < n.inputs.size(); ++i) if (!fact(n.carried[i]).value.known()) {
                    const Type& t = fact(n.carried[i]).type;
                    argument(aggregateType(t) ? "&("+aggregate(n.inputs[i],&t)+")" : normalize(scalarConversion(args[i],fact(n.inputs[i]).type,t,&fact(n.inputs[i]).value),t));
                }
                for (Id capture : m.environment(id)) argument("&("+variable("s",capture,m.slotType(capture))+")");
                std::set<Id> locations;
                for (Id receiver : n.receivers) for (Id offset : node(receiver).inputs) if (!fact(offset).value.known()) locations.insert(offset);
                for (Id offset : locations) argument(value(offset));
                if (aggregateType(f.type)) {
                    result = variable("v",id,f.type); argument("&"+result); line(call+");");
                } else result = assign(id, call + ")");
            } else result = value(n.expansion);
            break;
        }
        case Rule::Entry: case Rule::Step: case Rule::StateOf: state(id); break;
        default: throw Error(n.span, "unsupported residual node in MSX projection: " + ruleName(n.rule));
        }
        values[id] = result;
        return result;
    }
    std::string generate() {
        const auto& invocation = node(function.invocation);
        Type output = fact(function.invocation).type;
        std::string result;
        if (fact(function.invocation).value.known() && fact(function.invocation).effect < Effect::Control) {
            if (output.kind != Type::Empty) result = aggregateType(output) ? m.arrayConstant(function.invocation,output) : number(fact(function.invocation).value.number);
        } else result = value(invocation.expansion);
        if (aggregateType(output)) {
            if (result.empty()) result = aggregate(invocation.expansion,&output);
            copy("(*vx_result)",result,output);
        } else if (output.kind != Type::Empty) line("return " + normalize(result, output) + ";");
        std::ostringstream out;
        out << m.prototype(function, true) << " {\n";
        for (const auto& [name, type] : declarations) {
            bool recursive = m.recursiveFunctions.contains(function.invocation);
            out << "    " << (m.staticFrame && !recursive ? "static " : "") << m.typeName(type) << ' ' << name << ";\n";
            if (m.staticFrame && !recursive) m.frameBytes += storage(type);
        }
        for (std::size_t i = 0; i < function.parameters.size(); ++i) out << "    (void)p" << i << ";\n";
        out << code.str() << "}\n\n";
        return out.str();
    }
};

std::string Module::generate() {
    const auto& n = g.nodes[root.node];
    functions.push_back({root.node, "vxi_" + cName(root.name), n.inputs, {}, {}});
    materialized[root.node] = "vxi_" + cName(root.name);
    for (std::size_t i = 0; i < functions.size(); ++i) {
        Function f = functions[i]; // Materialization may grow the vector.
        prototypes << prototype(f, true) << ";\n";
        bodies << Body(*this, f).generate();
    }
    std::string result = "/* Derived from resolved Vexel facts. Override with user/vxp_" + root.name + ".c or .s. */\n#include \"vexel.h\"\n";
    for (const auto& [name, proto] : externals) { (void)name; result += proto + "\n"; }
    return result + typeDefinitions.str() + data.str() + prototypes.str() + bodies.str();
}
} // namespace

void installMsxFacts(Graph& g, const MsxOptions& options) {
    if (options.stackBytes < 128 || options.stackBytes > 8192)
        throw Error({}, "MSX stack reserve must be 128..8192 bytes");
    if (options.romSegments < 16 || options.romSegments > 16384)
        throw Error({}, "NEO-8 ROM capacity must be 16..16384 segments (megalinker emits at least 128 KiB)");
    const std::map<std::string, unsigned> facts = {
        {"__target_ram_bytes", 16384}, {"__target_rom_bank_bytes", 8192},
        {"__target_rom_segments", options.romSegments}, {"__target_stack_bytes", options.stackBytes},
        {"__target_resident_limit", 0xf380 - options.stackBytes},
        {"__target_ram_mapper_guaranteed_segments", 0},
        {"__target_scalar_max_bits",32}, {"__target_float16",1}, {"__target_float32",1}, {"__target_float64",0}
    };
    for (const auto& [name, value] : facts) {
        if (g.symbols.contains(name)) throw Error({}, "source conflicts with reserved target fact: " + name);
        for (const auto& n : g.nodes)
            if ((n.syntax == Syntax::Parameter || n.syntax == Syntax::Declare || n.syntax == Syntax::Name) && n.text == name) {
                if (n.syntax != Syntax::Name) throw Error(n.span, "cannot shadow target fact: " + name);
            }
        Node n;
        n.rule = Rule::Constant; n.text = name; n.identity = "target:msx-neo8/" + name;
        n.fact = Fact{Type::simple(Type::Exact), Value::integer(value), Effect::Pure};
        g.targetFacts[name] = g.add(std::move(n));
    }
}

void exportMsx(Graph& g, const std::vector<Root>& roots, const MsxOptions& options) {
    auto assets = options.assets.empty() ? std::filesystem::path(VEXEL_BACKEND_DIR) : options.assets;
    auto templateText = [&](const std::filesystem::path& path) {
        // Templates and the pinned linker are build inputs too; use the same
        // file snapshot/dependency service as source-language resources.
        Id file = g.read(path, false, {});
        return g.files[file].bytes;
    };
    if (std::filesystem::exists(options.directory) &&
        (!std::filesystem::is_directory(options.directory) || !std::filesystem::is_empty(options.directory)))
        throw Error({}, "export directory must be new or empty; existing projects are editable and will not be overwritten");
    std::map<std::string, std::string> files;
    std::ostringstream header, bridges, plan, modules;
    header << "#ifndef VEXEL_PROJECT_H\n#define VEXEL_PROJECT_H\n#include \"vx_runtime.h\"\n";
    bridges << "#include \"vexel.h\"\n";
    plan << "{\"format\":\"vexel-msx-plan-1\",\"target\":\"msx-neo8-bios-16k\","
         << "\"ram_start\":49152,\"stack_top\":62336,\"stack_bytes\":" << options.stackBytes
         << ",\"resident_limit\":" << 0xf380 - options.stackBytes
         << ",\"physical_ram_bytes\":16384,\"rom_segment_bytes\":8192,\"rom_segments\":" << options.romSegments
         << ",\"ram_mapper_guaranteed_segments\":0,\"calling_convention\":\"sdcccall(0)\","
         << "\"stack_proof\":\"unproven: reserve is a budget, not a bound\",\"functions\":[";
    std::set<std::string> remaining = options.exclusiveEntries;
    std::map<std::string, std::string> externalPrototypes;
    auto externalPrototype=[&](const std::string& name,const std::string& prototype) {
        auto [found,inserted]=externalPrototypes.emplace(name,prototype);
        if (!inserted && found->second!=prototype) throw Error({},"inconsistent external C signature: "+name);
    };
    std::map<std::string,std::string> externalStorage;
    std::set<Id> globalStorage;
    std::set<std::string> publicNames;
    for (const auto& root:roots) publicNames.insert(cName(root.name));
    for (const auto& root:roots)
        if (root.constant && g.nodes[root.node].fact->type.kind==Type::String && publicNames.contains(cName(root.name)+"_length"))
            throw Error(g.nodes[root.node].span,"export conflicts with generated string length symbol: "+root.name+"_length");
    bool first = true;
    std::string entry = options.entry;
    const Root* entryRoot = nullptr;
    for (const auto& root : roots) {
        const auto& n = g.nodes[root.node];
        const auto& f = *n.fact;
        std::string name = cName(root.name), module = "vxp_" + name;
        static const std::set<std::string> runtimeNames = {
            "fault", "segment_b", "trap", "bank_b", "msx_putchar", "signed",
            "mul", "divmod", "shift", "rescale", "fixed_mul", "fixed_div", "float_binary", "float_convert", "math", "last_result"
        };
        if (runtimeNames.contains(name) || name.starts_with("ext_"))
            throw Error(n.span, "export conflicts with reserved MSX C runtime name: " + name);
        if (root.constant) {
            if (scalar(f.type)) {
                (void)ctype(f.type);
                header << "extern const " << ctype(f.type) << " vx_" << name << ";\n";
                bridges << "const " << ctype(f.type) << " vx_" << name << " = " << number(f.value.number) << ";\n";
                continue;
            }
            if (aggregateType(f.type)) {
                Module output(g,root,false);
                output.arrayConstant(root.node,f.type,"vx_"+name);
                header << output.typeDefinitions.str() << "extern const " << output.typeName(f.type) << " vx_" << name << ";\n";
                bridges << output.data.str(); continue;
            }
            if (f.type.kind == Type::String) {
                header << "extern const uint8_t vx_" << name << '[' << std::max<std::size_t>(1,f.value.bytes.size()) << "];\n"
                       << "#define vx_" << name << "_length " << f.value.bytes.size() << "UL\n";
                bridges << "const uint8_t vx_" << name << "[] = {";
                for (std::size_t i=0;i<f.value.bytes.size();++i) {
                    if (i) bridges << ',';
                    bridges << static_cast<unsigned>(static_cast<unsigned char>(f.value.bytes[i]));
                }
                if (f.value.bytes.empty()) bridges << '0';
                bridges << "};\n"; continue;
            }
            throw Error(n.span,"MSX exported constant has no physical representation: "+f.type.str());
        }
        bool exclusive = options.exclusiveEntries.contains(name);
        remaining.erase(name);
        if (exclusive && f.effect == Effect::External)
            throw Error(n.span, "static frame requires a closed function without external callbacks: " + name);
        Module output(g, root, exclusive);
        files["generated/" + module + ".c"] = output.generate();
        globalStorage.insert(output.globalStorage.begin(),output.globalStorage.end());
        for (const auto& [symbol, proto] : output.externals) externalPrototype(symbol,proto);
        modules << ' ' << module;
        Function wrapper{root.node, "vx_" + name, n.inputs, {}, {}};
        std::string signature = output.prototype(wrapper, true) + " VX_RESIDENT";
        header << output.typeDefinitions.str();
        header << "/* Vexel result " << f.type.str() << "; arguments:";
        for (Id input : n.inputs) header << ' ' << g.nodes[input].fact->type.str();
        header << ". Fixed/float primitives use raw integer bits in C. */\n";
        header << signature << ";\n";
        bridges << "extern " << output.prototype(output.functions[0], true) << ";\n"
                << "#ifdef __SDCC\nextern const uint16_t __ML_SEGMENT_B_" << module << ";\n#endif\n"
                << signature << " {\n    uint16_t previous;\n";
        bool aggregateResult = aggregateType(f.type);
        if (f.type.kind != Type::Empty) bridges << "    " << output.typeName(f.type) << " result;\n";
        if (exclusive) bridges << "    static uint8_t busy;\n    if (busy) vx_trap(4);\n    busy = 1;\n";
        for (std::size_t i = 0; i < n.inputs.size(); ++i) {
            const auto& t = g.nodes[n.inputs[i]].fact->type;
            if (t.kind == Type::Boolean) bridges << "    if (p" << i << " > 1) vx_trap(5);\n";
        }
        bridges << "#ifdef __SDCC\n    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_" << module
                << ");\n#else\n    previous = vx_bank_b(0);\n#endif\n    ";
        if (f.type.kind != Type::Empty && !aggregateResult) bridges << "result = ";
        bridges << "vxi_" << name << '(';
        for (std::size_t i = 0; i < n.inputs.size(); ++i) {
            if (i) bridges << ", ";
            const Type& t = g.nodes[n.inputs[i]].fact->type;
            bridges << (aggregateType(t) ? "p"+std::to_string(i) : normalize("p" + std::to_string(i),t));
        }
        if (aggregateResult) bridges << (n.inputs.empty() ? "" : ", ") << "&result";
        bridges << ");\n    (void)vx_bank_b(previous);\n";
        if (exclusive) bridges << "    busy = 0;\n";
        if (aggregateResult) bridges << "    *vx_result = result;\n";
        else if (f.type.kind != Type::Empty) bridges << "    return result;\n";
        bridges << "}\n\n";
        if (!first) plan << ',';
        first = false;
        plan << "{\"name\":" << quote(name) << ",\"module\":" << quote(module) << ",\"window\":\"B\",\"window_address\":24576,"
             << "\"frame_policy\":" << quote(exclusive ? "static-exclusive" : "automatic")
             << ",\"exclusive_entry_assumption\":" << (exclusive ? "true" : "false")
             << ",\"static_temporaries_before_sdcc\":" << output.frameBytes
             << ",\"materialized_functions\":" << output.functions.size()
             << ",\"external_effects\":" << (f.effect == Effect::External ? "true" : "false") << '}';
        if (name == entry || (entry.empty() && n.inputs.empty() && (!entryRoot || name == "entry"))) entryRoot = &root;
    }
    if (!remaining.empty()) throw Error({}, "--msx-exclusive does not name an exported function: " + *remaining.begin());
    if (!entry.empty() && (!entryRoot || !g.nodes[entryRoot->node].inputs.empty()))
        throw Error({}, "--msx-entry must name a zero-argument exported function");
    if (entryRoot && aggregateType(g.nodes[entryRoot->node].fact->type))
        throw Error({},"sample MSX entry requires a scalar or no-value result; call aggregate exports from user/main.c");
    std::set<Id> emittedStorage;
    std::ostringstream bindings;bindings << '[';bool firstBinding=true;
    while (true) {
        auto pending=std::find_if(globalStorage.begin(),globalStorage.end(),[&](Id id){return !emittedStorage.contains(id);});
        if (pending==globalStorage.end()) break;
        Id id=*pending;emittedStorage.insert(id);
        const auto& definition=g.nodes[id];const auto& source=g.nodes[definition.site];const Type& type=definition.fact->type;
        Root storageRoot{"global_init_"+std::to_string(id),id,false,false};
        Module output(g,storageRoot,false);std::string physical=output.typeName(type),name=globalName(g,id),suffix=std::to_string(id);
        header << output.typeDefinitions.str();
        if (source.key=="backend-binding") {
            std::string kind;unsigned address=0;
            for (const auto& tag:source.metadata) {
                bool port=tag.starts_with("[[msx_port(");bool memory=tag.starts_with("[[msx_address(");
                if (!port && !memory) continue;
                if (!kind.empty()) throw Error(source.span,"backend storage requires exactly one MSX address or port annotation");
                auto begin=tag.find('(')+1,end=tag.find(')',begin);std::string spelling=tag.substr(begin,end-begin);
                std::size_t consumed=0;unsigned long parsed;
                try {parsed=std::stoul(spelling,&consumed,spelling.starts_with("0x") || spelling.starts_with("0X") ? 16 : 10);}
                catch (const std::exception&) {throw Error(source.span,"MSX storage address must be an unsigned integer literal");}
                while(consumed<spelling.size() && spelling[consumed]==' ') ++consumed;
                if (consumed!=spelling.size() || parsed>(port ? 255UL : 65535UL) || spelling.find('-')!=std::string::npos)
                    throw Error(source.span,"MSX storage address is outside its address space");
                kind=port ? "port" : "address";address=static_cast<unsigned>(parsed);
            }
            if (kind.empty()) throw Error(source.span,"backend-bound storage requires [[msx_address(N)]] or [[msx_port(N)]] on MSX");
            if (kind=="port" && !(type==Type::integer(8,false))) throw Error(source.span,"MSX port bindings require #u8");
            if (kind=="address" && storage(type)>65536-address) throw Error(source.span,"MSX address binding crosses the address-space boundary");
            header << "#ifdef __SDCC\n";
            if (kind=="port") header << "__sfr __at(" << address << ") " << name << ";\n";
            else header << "#define " << name << " (*(volatile " << physical << "*)" << address << "U)\n";
            header << "#else\nextern volatile " << physical << ' ' << name << "; /* Host binding shadow. */\n#endif\n";
            bridges << "#ifndef __SDCC\nvolatile " << physical << ' ' << name << ";\n#endif\n";
            if (!firstBinding) bindings << ',';
            firstBinding=false;
            bindings << "{\"name\":" << quote(source.text) << ",\"symbol\":" << quote(name) << ",\"space\":" << quote(kind)
                     << ",\"address\":" << address << ",\"bytes\":" << storage(type) << ",\"type\":" << quote(type.str()) << '}';
            continue;
        }
        header << "extern volatile " << physical << ' ' << name << ";\n";
        if (source.external) {
            auto [found,inserted]=externalStorage.emplace(name,physical);
            if (!inserted && found->second!=physical) throw Error(source.span,"inconsistent external C storage type: "+name);
            continue;
        }
        header << "extern volatile uint8_t vxg_state_" << suffix << ";\n";
        bridges << "volatile " << physical << ' ' << name << ";\nvolatile uint8_t vxg_state_" << suffix << ";\n";
        if (definition.expansion==none) continue;
        std::string module="vxp_"+storageRoot.name;
        files["generated/"+module+".c"]=output.generate();modules << ' ' << module;
        globalStorage.insert(output.globalStorage.begin(),output.globalStorage.end());
        for (const auto& [symbol,proto]:output.externals) externalPrototype(symbol,proto);
        header << "void vxg_ensure_" << suffix << "(void) VX_ABI VX_RESIDENT;\n";
        bridges << "extern " << output.prototype(output.functions[0],true) << ";\n"
                << "#ifdef __SDCC\nextern const uint16_t __ML_SEGMENT_B_" << module << ";\n#endif\n"
                << "void vxg_ensure_" << suffix << "(void) VX_ABI VX_RESIDENT {\n    uint16_t previous;\n    " << physical << " result;\n"
                << "    if (vxg_state_" << suffix << " == 2) return;\n    if (vxg_state_" << suffix << " == 1) vx_trap(8);\n"
                << "    vxg_state_" << suffix << " = 1;\n#ifdef __SDCC\n    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_" << module
                << ");\n#else\n    previous = vx_bank_b(0);\n#endif\n    ";
        if (!aggregateType(type)) bridges << "result = ";
        bridges << "vxi_" << storageRoot.name << '(' << (aggregateType(type) ? "&result" : "") << ");\n"
                << "    (void)vx_bank_b(previous);\n    " << name << " = result;\n    vxg_state_" << suffix << " = 2;\n}\n";
        if (!first) plan << ',';
        first=false;
        plan << "{\"name\":" << quote(storageRoot.name) << ",\"module\":" << quote(module)
             << ",\"window\":\"B\",\"window_address\":24576,\"frame_policy\":\"automatic\",\"initializer\":true,"
             << "\"exclusive_entry_assumption\":false,\"static_temporaries_before_sdcc\":0,\"materialized_functions\":" << output.functions.size()
             << ",\"external_effects\":true}";
    }
    for (const auto& [symbol, proto] : externalPrototypes) {
        if (externalStorage.contains(symbol)) throw Error({},"external C name denotes both storage and a function: "+symbol);
        header << proto << '\n';
    }
    header << "#endif\n";
    plan << "],\"storage_bindings\":" << bindings.str() << "],\"target_facts\":{";
    first = true;
    for (const auto& [name, id] : g.targetFacts) {
        if (!first) plan << ',';
        first = false; plan << quote(name) << ':' << g.nodes[id].fact->value.number;
    }
    plan << "},\"source_dependencies\":[";
    first = true;
    for (const auto& dep : g.dependencies) { if (!first) plan << ','; first = false; plan << quote(dep); }
    plan << "]}\n";
    files["generated/vexel.h"] = header.str();
    files["generated/vx_bridges.c"] = bridges.str();
    files["generated/plan.json"] = plan.str();
    std::ostringstream sourceMap;
    sourceMap << "{\"format\":\"vexel-msx-node-map-1\",\"nodes\":{";
    first = true;
    for (Id id = 0; id < g.nodes.size(); ++id) {
        const auto& node = g.nodes[id];
        if (node.rule == Rule::None) continue;
        if (!first) sourceMap << ',';
        first = false;
        sourceMap << quote(std::to_string(id)) << ":{\"source\":" << quote(g.location(node.span))
                  << ",\"rule\":" << quote(ruleName(node.rule))
                  << ",\"identity\":" << quote(node.identity);
        if (node.fact) sourceMap << ",\"type\":" << quote(node.fact->type.str());
        sourceMap << '}';
    }
    sourceMap << "}}\n";
    files["generated/node-map.json"] = sourceMap.str();
    std::ostringstream annotations;annotations << "{\"format\":\"vexel-annotations-1\",\"sites\":[";first=true;
    for (const auto& n : g.nodes) if (n.syntax!=Syntax::None && !n.metadata.empty()) {
        if (!first) annotations << ',';
        first=false;
        annotations << "{\"source\":" << quote(g.location(n.span)) << ",\"site\":" << quote(n.identity) << ",\"text\":[";
        for (std::size_t i=0;i<n.metadata.size();++i) {if(i)annotations<<',';annotations<<quote(n.metadata[i]);}
        annotations << "]}";
    }
    files["generated/annotations.json"]=annotations.str()+"]}\n";
    std::ostringstream processes;processes << "{\"format\":\"vexel-process-inputs-1\",\"inputs\":[";first=true;
    for (const auto& [request,result]:*g.processSnapshots) {
        if (!first) processes << ',';
        first=false;processes << "{\"request\":" << quote(request) << ",\"status\":" << quote(result.second)
                            << ",\"stdout\":" << quote(result.first) << '}';
    }
    files["generated/process-inputs.json"]=processes.str()+"]}\n";
    files["generated/modules.mk"] = "BANK_MODULES :=" + modules.str() + "\nROM_SEGMENTS := " + std::to_string(options.romSegments) + "\n";
    std::string main = "/* Editable application entry; this file is never refreshed by Vexel. */\n#include \"vexel.h\"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {\n";
    if (entryRoot) {
        main += "    ";
        if (g.nodes[entryRoot->node].fact->type.kind != Type::Empty) main += "vx_last_result = (uint32_t)";
        main += "vx_" + cName(entryRoot->name) + "();\n";
    } else main += "    /* Call an exported vx_* function here. */\n";
    main += "    for (;;) { __asm halt __endasm; }\n}\n";
    files["user/main.c"] = main;
    files["local.mk"] = "# Editable toolchain/optimization overrides. See README.md.\n";
    for (const std::string path : {"runtime/vx_runtime.h", "runtime/vx_runtime.c", "runtime/vx_fixed.c", "runtime/vx_float.c", "runtime/vx_math.c", "runtime/platform.s", "tools/check_project.py",
                                  "tools/neo8_runner.cpp", "tools/simulate.py", "vendor/chips/z80.h",
                                  "vendor/chips/LICENSE", "vendor/chips/UPSTREAM.md",
                                  "vendor/megalinker/megalinker.cc", "vendor/megalinker/LICENSE", "vendor/megalinker/UPSTREAM.md"})
        files[path] = templateText(assets / path);
    files["runtime/crt0.s"] = templateText(assets / "runtime/crt0.s.in");
    bool fixedRuntime = false;
    for (const auto& [path,contents] : files) if (path.starts_with("generated/") && path.ends_with(".c"))
        fixedRuntime = fixedRuntime || contents.find("vx_rescale(") != std::string::npos ||
            contents.find("vx_fixed_mul(") != std::string::npos || contents.find("vx_fixed_div(") != std::string::npos;
    if (fixedRuntime) files["runtime/vx_runtime.h"] = "#define VX_FIXED_RUNTIME 1\n" + files["runtime/vx_runtime.h"];
    files["generated/modules.mk"] += std::string("FIXED_RUNTIME := ") + (fixedRuntime ? "1\n" : "0\n");
    bool floatRuntime = false;
    for (const auto& [path,contents] : files) if (path.starts_with("generated/") && path.ends_with(".c"))
        floatRuntime = floatRuntime || contents.find("vx_float_") != std::string::npos;
    if (floatRuntime) files["runtime/vx_runtime.h"] = "#define VX_FLOAT_RUNTIME 1\n" + files["runtime/vx_runtime.h"];
    files["generated/modules.mk"] += std::string("FLOAT_RUNTIME := ") + (floatRuntime ? "1\n" : "0\n");
    bool mathRuntime=false;
    for(const auto& [path,contents]:files) if(path.starts_with("generated/") && path.ends_with(".c"))
        mathRuntime=mathRuntime || contents.find("vx_math(")!=std::string::npos;
    if(mathRuntime) files["runtime/vx_runtime.h"]="#define VX_MATH_RUNTIME 1\n"+files["runtime/vx_runtime.h"];
    files["generated/modules.mk"]+=std::string("MATH_RUNTIME := ")+(mathRuntime ? "1\n" : "0\n");
    files["Makefile"] = templateText(assets / "Project.mk");
    files["README.md"] = templateText(assets / "Project.md");
    std::string fingerprints = "{";
    for (const auto& [path, contents] : files) {
        if (fingerprints.size() > 1) fingerprints += ',';
        fingerprints += quote(path) + ":" + quote(fingerprint(contents));
    }
    files["generated/source-fingerprints.json"] = fingerprints + "}\n";
    // Validate and render everything before touching an export directory.
    for (const auto& [path, contents] : files) put(options.directory / path, contents);
}
} // namespace vx
