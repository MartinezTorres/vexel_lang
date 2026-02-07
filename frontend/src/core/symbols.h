#pragma once
#include "ast.h"
#include <unordered_map>
#include <unordered_set>

namespace vexel {

struct Symbol {
    enum class Kind { Variable, Function, Type, Constant };
    Kind kind;
    std::string name;
    TypePtr type = nullptr;
    bool is_mutable = false;
    bool is_external = false;
    bool is_exported = false;
    StmtPtr declaration = nullptr;
    int module_id = -1;
    int instance_id = -1;
    bool is_local = false;
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, Symbol*> symbols;
    int id;

    Scope(Scope* p = nullptr, int scope_id = 0) : parent(p), id(scope_id) {}

    Symbol* lookup(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }

    void define(const std::string& name, Symbol* sym) {
        if (symbols.count(name)) {
            throw CompileError("Name already defined: " + name, SourceLocation());
        }
        symbols[name] = sym;
    }

    bool exists_in_current(const std::string& name) {
        return symbols.count(name) > 0;
    }
};

} // namespace vexel
