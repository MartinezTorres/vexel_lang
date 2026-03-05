#pragma once
#include "ast.h"
#include <unordered_map>
#include <unordered_set>

namespace vexel {

struct Symbol {
    enum class Kind { Variable, Function, Type, Constant };
    Kind kind;
    std::string name;
    std::string surface_name;
    TypePtr type = nullptr;
    bool is_mutable = false;
    bool is_external = false;
    bool is_backend_bound = false;
    bool is_exported = false;
    bool is_resource_binding = false;
    StmtPtr declaration = nullptr;
    int module_id = -1;
    int instance_id = -1;
    bool is_local = false;
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, Symbol*> internal_symbols;
    std::unordered_map<std::string, Symbol*> value_symbols;
    std::unordered_map<std::string, Symbol*> type_symbols;
    std::unordered_map<std::string, std::vector<Symbol*>> function_overloads;
    int id;

    Scope(Scope* p = nullptr, int scope_id = 0) : parent(p), id(scope_id) {}

    Symbol* lookup(const std::string& name) {
        if (Symbol* sym = lookup_internal(name)) return sym;
        if (Symbol* sym = lookup_value(name)) return sym;
        if (Symbol* sym = lookup_type(name)) return sym;
        std::vector<Symbol*> overloads = lookup_functions(name);
        if (overloads.size() == 1) return overloads.front();
        return nullptr;
    }

    Symbol* lookup_internal(const std::string& name) {
        auto it = internal_symbols.find(name);
        if (it != internal_symbols.end()) return it->second;
        if (parent) return parent->lookup_internal(name);
        return nullptr;
    }

    Symbol* lookup_value(const std::string& name) {
        auto it = value_symbols.find(name);
        if (it != value_symbols.end()) return it->second;
        if (parent) return parent->lookup_value(name);
        return nullptr;
    }

    Symbol* lookup_type(const std::string& name) {
        auto it = type_symbols.find(name);
        if (it != type_symbols.end()) return it->second;
        if (parent) return parent->lookup_type(name);
        return nullptr;
    }

    std::vector<Symbol*> lookup_functions(const std::string& name) {
        if (value_symbols.count(name) > 0) {
            return {};
        }
        auto it = function_overloads.find(name);
        if (it != function_overloads.end()) return it->second;
        if (parent) return parent->lookup_functions(name);
        return {};
    }

    void define_value(const std::string& name, Symbol* sym) {
        if (value_symbols.count(name) || function_overloads.count(name)) {
            throw CompileError("Name already defined: " + name, SourceLocation());
        }
        auto internal_it = internal_symbols.find(sym->name);
        if (internal_it != internal_symbols.end() && internal_it->second != sym) {
            throw CompileError("Name already defined: " + sym->name, SourceLocation());
        }
        value_symbols[name] = sym;
        internal_symbols[sym->name] = sym;
    }

    void define_type(const std::string& name, Symbol* sym) {
        if (type_symbols.count(name)) {
            throw CompileError("Name already defined: " + name, SourceLocation());
        }
        auto internal_it = internal_symbols.find(sym->name);
        if (internal_it != internal_symbols.end() && internal_it->second != sym) {
            throw CompileError("Name already defined: " + sym->name, SourceLocation());
        }
        type_symbols[name] = sym;
        internal_symbols[sym->name] = sym;
    }

    void define_function(const std::string& name, Symbol* sym) {
        if (value_symbols.count(name)) {
            throw CompileError("Name already defined: " + name, SourceLocation());
        }
        auto internal_it = internal_symbols.find(sym->name);
        if (internal_it != internal_symbols.end() && internal_it->second != sym) {
            throw CompileError("Name already defined: " + sym->name, SourceLocation());
        }
        function_overloads[name].push_back(sym);
        internal_symbols[sym->name] = sym;
    }

    void define(const std::string& name, Symbol* sym) {
        switch (sym->kind) {
            case Symbol::Kind::Function:
                define_function(name, sym);
                return;
            case Symbol::Kind::Type:
                define_type(name, sym);
                return;
            case Symbol::Kind::Variable:
            case Symbol::Kind::Constant:
                define_value(name, sym);
                return;
        }
    }

    bool exists_value_in_current(const std::string& name) const {
        return value_symbols.count(name) > 0;
    }

    bool exists_type_in_current(const std::string& name) const {
        return type_symbols.count(name) > 0;
    }

    bool exists_function_in_current(const std::string& name) const {
        return function_overloads.count(name) > 0;
    }

    bool exists_internal_in_current(const std::string& name) const {
        return internal_symbols.count(name) > 0;
    }

    bool exists_in_current(const std::string& name) {
        return exists_value_in_current(name) ||
               exists_type_in_current(name) ||
               exists_function_in_current(name) ||
               exists_internal_in_current(name);
    }

    bool has_visible_name_in_current(const std::string& name) const {
        return exists_value_in_current(name) ||
               exists_type_in_current(name) ||
               exists_function_in_current(name);
    }

    bool has_any_visible_name(const std::string& name) const {
        if (has_visible_name_in_current(name)) return true;
        if (parent) return parent->has_any_visible_name(name);
        return false;
    }
};

} // namespace vexel
