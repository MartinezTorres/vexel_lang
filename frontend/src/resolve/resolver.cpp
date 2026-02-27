#include "resolver.h"
#include "ast_walk.h"
#include "expr_access.h"
#include "path_utils.h"
#include <algorithm>
#include <filesystem>

namespace vexel {

namespace {
unsigned long long stmt_key(int instance_id, const Stmt* stmt) {
    return (static_cast<unsigned long long>(static_cast<uint32_t>(instance_id)) << 32) ^
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stmt));
}

long long scope_module_key(int scope_id, int module_id) {
    return (static_cast<long long>(scope_id) << 32) ^ static_cast<unsigned long long>(module_id);
}

std::string qualified_name_for_func(StmtPtr stmt) {
    if (!stmt) return "";
    if (stmt->type_namespace.empty()) return stmt->func_name;
    return stmt->type_namespace + "::" + stmt->func_name;
}

std::string qualified_import_prefix(const std::vector<std::string>& import_path) {
    std::string out;
    for (size_t i = 0; i < import_path.size(); ++i) {
        if (i > 0) out += "::";
        out += import_path[i];
    }
    return out;
}

std::string overload_internal_name(const std::string& surface_name, size_t ordinal, bool disambiguate_first) {
    if (ordinal == 0 && !disambiguate_first) return surface_name;
    return surface_name + "__ov" + std::to_string(ordinal);
}
}

Resolver::Resolver(Program& program_in, Bindings& bindings_in, const std::string& root)
    : program(program_in),
      bindings(bindings_in),
      current_scope(nullptr),
      scope_counter(0),
      project_root(root),
      current_module(nullptr),
      current_instance_id(-1),
      current_module_id(-1) {}

Scope* Resolver::instance_scope(int instance_id) const {
    auto it = instance_scopes.find(instance_id);
    if (it == instance_scopes.end()) return nullptr;
    return it->second;
}

Symbol* Resolver::lookup_internal_in_instance(int instance_id, const std::string& name) const {
    Scope* scope = instance_scope(instance_id);
    if (!scope) return nullptr;
    return scope->lookup_internal(name);
}

Symbol* Resolver::lookup_value_in_instance(int instance_id, const std::string& name) const {
    Scope* scope = instance_scope(instance_id);
    if (!scope) return nullptr;
    return scope->lookup_value(name);
}

Symbol* Resolver::lookup_type_in_instance(int instance_id, const std::string& name) const {
    Scope* scope = instance_scope(instance_id);
    if (!scope) return nullptr;
    return scope->lookup_type(name);
}

std::vector<Symbol*> Resolver::lookup_functions_in_instance(int instance_id, const std::string& name) const {
    Scope* scope = instance_scope(instance_id);
    if (!scope) return {};
    return scope->lookup_functions(name);
}

void Resolver::resolve() {
    if (program.modules.empty()) return;
    build_module_imports();

    int root_scope_id = scope_counter++;
    ModuleInstance& entry = get_or_create_instance(program.modules.front().id, root_scope_id, SourceLocation());
    resolve_instance(entry.id);
}

void Resolver::resolve_generated_function(StmtPtr func, int instance_id) {
    if (!func || func->kind != Stmt::Kind::FuncDecl) return;

    ModuleInstance& inst = program.instances[static_cast<size_t>(instance_id)];
    std::string surface_name = qualified_name_for_func(func);
    std::string internal_name = surface_name;

    if (inst.symbols.count(internal_name)) {
        throw CompileError("Name already defined: " + internal_name, func->location);
    }

    Symbol* sym = create_symbol(Symbol::Kind::Function, internal_name, func, false, false, surface_name);
    sym->is_external = func->is_external;
    sym->is_exported = func->is_exported;
    sym->module_id = inst.module_id;
    sym->instance_id = inst.id;

    inst.symbols[internal_name] = sym;
    inst.function_overloads[surface_name].push_back(sym);
    Scope* scope = instance_scope(instance_id);
    if (scope) {
        scope->define_function(surface_name, sym);
    }
    bindings.bind(instance_id, func.get(), sym);

    Scope* saved_scope = current_scope;
    int saved_instance = current_instance_id;
    int saved_module = current_module_id;

    current_scope = scope;
    current_instance_id = instance_id;
    current_module_id = inst.module_id;

    resolve_func_decl(func, false);

    current_scope = saved_scope;
    current_instance_id = saved_instance;
    current_module_id = saved_module;
}

void Resolver::push_scope(int forced_id) {
    int id = forced_id >= 0 ? forced_id : scope_counter++;
    if (forced_id >= 0 && scope_counter <= forced_id) {
        scope_counter = forced_id + 1;
    }
    scopes.push_back(std::make_unique<Scope>(current_scope, id));
    current_scope = scopes.back().get();
}

void Resolver::pop_scope() {
    if (current_scope && current_scope->parent) {
        current_scope = current_scope->parent;
    }
}

void Resolver::verify_no_shadowing(const std::string& name, Symbol::Kind kind, const SourceLocation& loc) {
    if (name == "_") return;
    if (!current_scope) return;
    switch (kind) {
        case Symbol::Kind::Type:
            if (current_scope->lookup_type(name)) {
                throw CompileError("Name shadows existing definition: " + name, loc);
            }
            break;
        case Symbol::Kind::Function:
        case Symbol::Kind::Variable:
        case Symbol::Kind::Constant:
            if (current_scope->lookup_value(name)) {
                throw CompileError("Name shadows existing definition: " + name, loc);
            }
            if (kind == Symbol::Kind::Function) {
                std::vector<Symbol*> overloads = current_scope->lookup_functions(name);
                if (!overloads.empty() && !current_scope->exists_function_in_current(name)) {
                    throw CompileError("Name shadows existing definition: " + name, loc);
                }
            } else if (!current_scope->lookup_functions(name).empty()) {
                throw CompileError("Name shadows existing definition: " + name, loc);
            }
            break;
    }
}

void Resolver::resolve_instance(int instance_id) {
    if (resolved_instances.count(instance_id)) return;
    resolved_instances.insert(instance_id);

    ModuleInstance& instance = program.instances[static_cast<size_t>(instance_id)];
    ModuleInfo* mod_info = program.module(instance.module_id);
    if (!mod_info) {
        throw CompileError("Internal error: missing module for instance", SourceLocation());
    }

    Module* saved_module = current_module;
    int saved_instance = current_instance_id;
    int saved_module_id = current_module_id;
    Scope* saved_scope = current_scope;

    current_module = &mod_info->module;
    current_instance_id = instance.id;
    current_module_id = instance.module_id;

    current_scope = nullptr;
    push_scope(instance.scope_id);
    instance_scopes[instance.id] = current_scope;

    for (const auto& pair : instance.value_symbols) {
        current_scope->define_value(pair.first, pair.second);
    }
    for (const auto& pair : instance.type_symbols) {
        current_scope->define_type(pair.first, pair.second);
    }
    for (const auto& pair : instance.function_overloads) {
        for (Symbol* sym : pair.second) {
            current_scope->define_function(pair.first, sym);
        }
    }

    for (size_t i = 0; i < current_module->top_level.size(); ++i) {
        resolve_stmt(current_module->top_level[i]);
    }

    auto pending_it = pending_imports.find(instance.id);
    if (pending_it != pending_imports.end()) {
        std::vector<int> pending = std::move(pending_it->second);
        pending_imports.erase(pending_it);
        for (int pending_id : pending) {
            resolve_instance(pending_id);
        }
    }

    pop_scope();
    current_scope = saved_scope;
    current_module = saved_module;
    current_instance_id = saved_instance;
    current_module_id = saved_module_id;
}

void Resolver::predeclare_instance_symbols(ModuleInstance& instance) {
    ModuleInfo* mod_info = program.module(instance.module_id);
    if (!mod_info) return;
    std::unordered_map<std::string, size_t> function_counts;
    for (const auto& stmt : mod_info->module.top_level) {
        if (!stmt || stmt->kind != Stmt::Kind::FuncDecl) continue;
        function_counts[qualified_name_for_func(stmt)] += 1;
    }
    std::unordered_map<std::string, size_t> function_ordinals;
    for (const auto& stmt : mod_info->module.top_level) {
        if (!stmt) continue;
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            std::string surface_name = qualified_name_for_func(stmt);
            size_t ordinal = function_ordinals[surface_name]++;
            if (function_counts[surface_name] > 1 && (stmt->is_external || stmt->is_exported)) {
                throw CompileError("ABI-visible function overloads are not supported: " + surface_name,
                                   stmt->location);
            }
            bool disambiguate_first = ordinal == 0 && instance.symbols.count(surface_name);
            std::string internal_name = overload_internal_name(surface_name, ordinal, disambiguate_first);
            if (instance.symbols.count(internal_name)) {
                throw CompileError("Name already defined: " + internal_name, stmt->location);
            }
            Symbol* sym = create_symbol(Symbol::Kind::Function,
                                        internal_name,
                                        stmt,
                                        false,
                                        false,
                                        surface_name);
            sym->is_external = stmt->is_external;
            sym->is_exported = stmt->is_exported;
            sym->module_id = instance.module_id;
            sym->instance_id = instance.id;
            instance.symbols[internal_name] = sym;
            instance.function_overloads[surface_name].push_back(sym);
            bindings.bind(instance.id, stmt.get(), sym);
        } else if (stmt->kind == Stmt::Kind::TypeDecl) {
            if (instance.type_symbols.count(stmt->type_decl_name)) {
                throw CompileError("Name already defined: " + stmt->type_decl_name, stmt->location);
            }
            Symbol* sym = create_symbol(Symbol::Kind::Type,
                                        stmt->type_decl_name,
                                        stmt,
                                        false,
                                        false,
                                        stmt->type_decl_name);
            sym->module_id = instance.module_id;
            sym->instance_id = instance.id;
            instance.symbols[stmt->type_decl_name] = sym;
            instance.type_symbols[stmt->type_decl_name] = sym;
            bindings.bind(instance.id, stmt.get(), sym);
        } else if (stmt->kind == Stmt::Kind::VarDecl) {
            if (instance.value_symbols.count(stmt->var_name) || instance.function_overloads.count(stmt->var_name)) {
                throw CompileError("Name already defined: " + stmt->var_name, stmt->location);
            }
            Symbol* sym = create_symbol(stmt->is_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant,
                                        stmt->var_name,
                                        stmt,
                                        stmt->is_mutable,
                                        false,
                                        stmt->var_name);
            sym->is_external = stmt->var_linkage != VarLinkageKind::Normal;
            sym->is_backend_bound = stmt->var_linkage == VarLinkageKind::BackendBound;
            sym->is_exported = stmt->is_exported;
            sym->module_id = instance.module_id;
            sym->instance_id = instance.id;
            instance.symbols[stmt->var_name] = sym;
            instance.value_symbols[stmt->var_name] = sym;
            bindings.bind(instance.id, stmt.get(), sym);
        }
    }
}

Symbol* Resolver::create_symbol(Symbol::Kind kind,
                                const std::string& name,
                                StmtPtr decl,
                                bool is_mutable,
                                bool is_local,
                                const std::string& surface_name) {
    auto sym = std::make_unique<Symbol>();
    sym->kind = kind;
    sym->name = name;
    sym->surface_name = surface_name.empty() ? name : surface_name;
    sym->is_mutable = is_mutable;
    sym->declaration = decl;
    sym->is_local = is_local;
    Symbol* out = sym.get();
    program.symbols.push_back(std::move(sym));
    return out;
}

ModuleInstance& Resolver::get_or_create_instance(int module_id, int scope_id, const SourceLocation& loc) {
    (void)loc;
    long long key = scope_module_key(scope_id, module_id);
    auto it = instance_by_scope_module.find(key);
    if (it != instance_by_scope_module.end()) {
        return program.instances[static_cast<size_t>(it->second)];
    }

    ModuleInstance instance;
    instance.id = static_cast<ModuleInstanceId>(program.instances.size());
    instance.module_id = module_id;
    instance.scope_id = scope_id;
    program.instances.push_back(std::move(instance));
    instance_by_scope_module[key] = program.instances.back().id;

    predeclare_instance_symbols(program.instances.back());

    return program.instances.back();
}

void Resolver::resolve_stmt(StmtPtr stmt) {
    if (!stmt) return;
    unsigned long long key = stmt_key(current_instance_id, stmt.get());
    if (resolved_statements.count(key)) return;
    resolved_statements.insert(key);

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            resolve_func_decl(stmt, current_scope && current_scope->parent != nullptr);
            break;
        case Stmt::Kind::TypeDecl:
            resolve_type_decl(stmt);
            break;
        case Stmt::Kind::VarDecl:
            resolve_var_decl(stmt);
            break;
        case Stmt::Kind::Import:
            handle_import(stmt);
            break;
        case Stmt::Kind::Expr:
            resolve_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            resolve_expr(stmt->return_expr);
            break;
        case Stmt::Kind::ConditionalStmt:
            resolve_expr(stmt->condition);
            resolve_stmt(stmt->true_stmt);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

void Resolver::resolve_func_decl(StmtPtr stmt, bool define_symbol) {
    if (!stmt || stmt->kind != Stmt::Kind::FuncDecl) return;

    std::string func_name = qualified_name_for_func(stmt);

    if (define_symbol) {
        verify_no_shadowing(func_name, Symbol::Kind::Function, stmt->location);
        size_t ordinal = current_scope->exists_function_in_current(func_name)
                             ? current_scope->function_overloads[func_name].size()
                             : 0;
        bool disambiguate_first = ordinal == 0 && current_scope->exists_internal_in_current(func_name);
        std::string internal_name = overload_internal_name(func_name, ordinal, disambiguate_first);
        if ((stmt->is_external || stmt->is_exported) && current_scope->exists_function_in_current(func_name)) {
            throw CompileError("ABI-visible function overloads are not supported: " + func_name, stmt->location);
        }
        Symbol* sym = create_symbol(Symbol::Kind::Function,
                                    internal_name,
                                    stmt,
                                    false,
                                    false,
                                    func_name);
        sym->is_external = stmt->is_external;
        sym->is_exported = stmt->is_exported;
        sym->module_id = current_module_id;
        sym->instance_id = current_instance_id;
        current_scope->define_function(func_name, sym);
        bindings.bind(current_instance_id, stmt.get(), sym);
    }

    for (auto& param : stmt->params) {
        if (param.type) {
            resolve_type(param.type);
        }
    }
    for (auto& rt : stmt->return_types) {
        resolve_type(rt);
    }
    if (stmt->return_type) {
        resolve_type(stmt->return_type);
    }

    if (stmt->is_external || !stmt->body) {
        return;
    }

    push_scope();

    for (const auto& ref_param : stmt->ref_params) {
        if (current_scope->exists_value_in_current(ref_param) ||
            current_scope->exists_function_in_current(ref_param)) {
            throw CompileError("Name already defined: " + ref_param, stmt->location);
        }
        Symbol* sym = create_symbol(Symbol::Kind::Variable, ref_param, stmt, true, true);
        sym->module_id = current_module_id;
        sym->instance_id = current_instance_id;
        current_scope->define_value(ref_param, sym);
        bindings.bind(current_instance_id, &ref_param, sym);
    }

    for (auto& param : stmt->params) {
        if (current_scope->exists_value_in_current(param.name) ||
            current_scope->exists_function_in_current(param.name)) {
            throw CompileError("Name already defined: " + param.name, param.location);
        }
        Symbol* sym = create_symbol(Symbol::Kind::Variable, param.name, stmt, false, true);
        sym->module_id = current_module_id;
        sym->instance_id = current_instance_id;
        current_scope->define_value(param.name, sym);
        bindings.bind(current_instance_id, &param, sym);
    }

    resolve_expr(stmt->body);

    pop_scope();
}

void Resolver::resolve_type_decl(StmtPtr stmt) {
    if (!stmt || stmt->kind != Stmt::Kind::TypeDecl) return;

    if (current_scope) {
        Symbol* existing = current_scope->lookup_type(stmt->type_decl_name);
        if (existing && !existing->is_local) {
            bindings.bind(current_instance_id, stmt.get(), existing);
        } else {
            verify_no_shadowing(stmt->type_decl_name, Symbol::Kind::Type, stmt->location);
            Symbol* sym = create_symbol(Symbol::Kind::Type, stmt->type_decl_name, stmt, false,
                                        current_scope->parent != nullptr);
            sym->module_id = current_module_id;
            sym->instance_id = current_instance_id;
            current_scope->define_type(stmt->type_decl_name, sym);
            bindings.bind(current_instance_id, stmt.get(), sym);
        }
    }

    for (auto& field : stmt->fields) {
        if (field.type) {
            resolve_type(field.type);
        }
    }
}

void Resolver::resolve_var_decl(StmtPtr stmt) {
    if (!stmt || stmt->kind != Stmt::Kind::VarDecl) return;

    if (stmt->var_type) {
        resolve_type(stmt->var_type);
    }

    if (stmt->var_init) {
        resolve_expr(stmt->var_init);
    }

    if (!current_scope) return;

    Symbol* sym = nullptr;
    Symbol* existing = current_scope->lookup_value(stmt->var_name);
    if (existing && !existing->is_local) {
        sym = existing;
        bindings.bind(current_instance_id, stmt.get(), existing);
    } else {
        verify_no_shadowing(stmt->var_name,
                            stmt->is_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant,
                            stmt->location);

        sym = create_symbol(stmt->is_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant,
                            stmt->var_name,
                            stmt,
                            stmt->is_mutable,
                            current_scope->parent != nullptr);
        sym->module_id = current_module_id;
        sym->instance_id = current_instance_id;
        current_scope->define_value(stmt->var_name, sym);
        bindings.bind(current_instance_id, stmt.get(), sym);
    }

    if (sym && !sym->is_local) {
        sym->is_external = stmt->var_linkage != VarLinkageKind::Normal;
        sym->is_backend_bound = stmt->var_linkage == VarLinkageKind::BackendBound;
        sym->is_exported = stmt->is_exported;
        defined_globals.insert(sym);
    } else if (sym) {
        sym->is_external = stmt->var_linkage != VarLinkageKind::Normal;
        sym->is_backend_bound = stmt->var_linkage == VarLinkageKind::BackendBound;
    }
}

void Resolver::resolve_expr(ExprPtr expr) {
    if (!expr) return;

    switch (expr->kind) {
        case Expr::Kind::Identifier: {
            Symbol* sym = current_scope ? current_scope->lookup_value(expr->name) : nullptr;
            if (!sym) {
                // Binding-only resolver: unresolved identifiers are deferred to type checking.
                break;
            }
            if (!sym->is_local &&
                (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant)) {
                bool requires_definition = sym->declaration && sym->declaration->var_init;
                if (requires_definition && !defined_globals.count(sym)) {
                    throw CompileError("Undefined identifier: " + expr->name, expr->location);
                }
            }
            if (expr->type) {
                resolve_type(expr->type);
            }
            bindings.bind(current_instance_id, expr.get(), sym);
            break;
        }
        case Expr::Kind::Binary:
            resolve_expr(expr->left);
            resolve_expr(expr->right);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Length:
            resolve_expr(expr->operand);
            break;
        case Expr::Kind::Cast:
            resolve_expr(expr->operand);
            if (expr->target_type) {
                resolve_type(expr->target_type);
            }
            break;
        case Expr::Kind::Call:
            if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                Symbol* sym = nullptr;
                if (current_scope) {
                    if (expr->is_constructor_call) {
                        sym = current_scope->lookup_type(expr->operand->name);
                    } else {
                        std::vector<Symbol*> overloads = current_scope->lookup_functions(expr->operand->name);
                        if (overloads.size() == 1) {
                            sym = overloads.front();
                        }
                    }
                }
                if (sym) {
                    bindings.bind(current_instance_id, expr->operand.get(), sym);
                }
            } else {
                resolve_expr(expr->operand);
            }
            for (auto& rec : expr->receivers) {
                resolve_expr(rec);
            }
            for (auto& arg : expr->args) {
                resolve_expr(arg);
            }
            break;
        case Expr::Kind::Index:
            resolve_expr(expr->operand);
            if (!expr->args.empty()) {
                resolve_expr(expr->args[0]);
            }
            break;
        case Expr::Kind::Member:
            resolve_expr(expr->operand);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (auto& elem : expr->elements) {
                resolve_expr(elem);
            }
            break;
        case Expr::Kind::Block:
            push_scope();
            try {
                for (auto& st : expr->statements) {
                    resolve_stmt(st);
                }
                resolve_expr(expr->result_expr);
            } catch (const CompileError& err) {
                pop_scope();
                if (expr->is_optional_semantic_block && is_optional_semantic_failure(err)) {
                    expr->statements.clear();
                    expr->result_expr = nullptr;
                    expr->is_optional_semantic_block = false;
                    break;
                }
                throw;
            }
            pop_scope();
            break;
        case Expr::Kind::Conditional:
            resolve_expr(expr->condition);
            resolve_expr(expr->true_expr);
            resolve_expr(expr->false_expr);
            break;
        case Expr::Kind::Assignment: {
            if (expr->left && expr->left->kind == Expr::Kind::Identifier) {
                Symbol* sym = current_scope ? current_scope->lookup_value(expr->left->name) : nullptr;
                if (!sym) {
                    resolve_expr(expr->right);
                    if (expr->left->type) {
                        resolve_type(expr->left->type);
                    }
                    verify_no_shadowing(expr->left->name, Symbol::Kind::Variable, expr->location);

                    Symbol* new_sym = create_symbol(Symbol::Kind::Variable, expr->left->name, nullptr, true, true);
                    new_sym->module_id = current_module_id;
                    new_sym->instance_id = current_instance_id;
                    current_scope->define_value(expr->left->name, new_sym);
                    bindings.bind(current_instance_id, expr->left.get(), new_sym);
                    bindings.set_new_variable(current_instance_id, expr.get(), true);
                    break;
                }
                bindings.bind(current_instance_id, expr->left.get(), sym);
                bindings.set_new_variable(current_instance_id, expr.get(), false);
            }
            if (expr->left && expr->left->kind != Expr::Kind::Identifier) {
                resolve_expr(expr->left);
            }
            resolve_expr(expr->right);
            break;
        }
        case Expr::Kind::Range:
            resolve_expr(expr->left);
            resolve_expr(expr->right);
            break;
        case Expr::Kind::Iteration:
            resolve_expr(loop_subject(expr));
            push_scope();
            {
                Symbol* sym = create_symbol(Symbol::Kind::Variable, "_", nullptr, false, true);
                sym->module_id = current_module_id;
                sym->instance_id = current_instance_id;
                current_scope->define_value("_", sym);
            }
            resolve_expr(loop_body(expr));
            pop_scope();
            break;
        case Expr::Kind::Repeat:
            resolve_expr(loop_subject(expr));
            resolve_expr(loop_body(expr));
            break;
        case Expr::Kind::Resource:
        case Expr::Kind::Process:
            break;
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
            break;
    }
}

void Resolver::resolve_type(TypePtr type) {
    if (!type) return;

    switch (type->kind) {
        case Type::Kind::Array:
            resolve_type(type->element_type);
            if (type->array_size) {
                resolve_expr(type->array_size);
            }
            break;
        case Type::Kind::Vector:
            resolve_type(type->element_type);
            if (type->array_size) {
                resolve_expr(type->array_size);
            }
            break;
        case Type::Kind::Matrix:
            resolve_type(type->element_type);
            if (type->array_size) {
                resolve_expr(type->array_size);
            }
            if (type->matrix_cols) {
                resolve_expr(type->matrix_cols);
            }
            break;
        case Type::Kind::Named: {
            Symbol* sym = current_scope ? current_scope->lookup_type(type->type_name) : nullptr;
            if (sym && sym->kind == Symbol::Kind::Type) {
                bindings.bind(current_instance_id, type.get(), sym);
            }
            break;
        }
        case Type::Kind::Primitive:
        case Type::Kind::TypeVar:
            break;
        case Type::Kind::TypeOf:
            if (type->typeof_expr) {
                resolve_expr(type->typeof_expr);
            }
            break;
    }
}

void Resolver::handle_import(StmtPtr stmt) {
    if (!stmt || !current_scope) return;

    std::string resolved_path;
    if (!try_resolve_module_path(stmt->import_path, stmt->location.filename, resolved_path)) {
        throw CompileError("Import failed: cannot resolve module", stmt->location);
    }

    auto it = program.path_to_id.find(resolved_path);
    if (it == program.path_to_id.end()) {
        throw CompileError("Import failed: module not found", stmt->location);
    }

    int module_id = it->second;

    if (scope_loaded_modules[current_scope].count(module_id)) {
        return;
    }
    scope_loaded_modules[current_scope].insert(module_id);

    ModuleInstance& instance = get_or_create_instance(module_id, current_scope->id, stmt->location);

    std::string module_prefix = qualified_import_prefix(stmt->import_path);
    for (const auto& pair : instance.value_symbols) {
        if (current_scope->has_visible_name_in_current(pair.first)) {
            throw CompileError("Name already defined: " + pair.first, stmt->location);
        }
        current_scope->define_value(pair.first, pair.second);
        if (!module_prefix.empty()) {
            std::string qualified = module_prefix + "::" + pair.first;
            if (!current_scope->has_visible_name_in_current(qualified)) {
                current_scope->define_value(qualified, pair.second);
            }
        }
    }
    for (const auto& pair : instance.type_symbols) {
        if (current_scope->exists_type_in_current(pair.first)) {
            throw CompileError("Name already defined: " + pair.first, stmt->location);
        }
        current_scope->define_type(pair.first, pair.second);
        if (!module_prefix.empty()) {
            std::string qualified = module_prefix + "::" + pair.first;
            if (!current_scope->exists_type_in_current(qualified)) {
                current_scope->define_type(qualified, pair.second);
            }
        }
    }
    for (const auto& pair : instance.function_overloads) {
        if (current_scope->exists_value_in_current(pair.first)) {
            throw CompileError("Name already defined: " + pair.first, stmt->location);
        }
        for (Symbol* sym : pair.second) {
            current_scope->define_function(pair.first, sym);
        }
        if (!module_prefix.empty()) {
            std::string qualified = module_prefix + "::" + pair.first;
            if (current_scope->exists_value_in_current(qualified)) {
                throw CompileError("Name already defined: " + qualified, stmt->location);
            }
            for (Symbol* sym : pair.second) {
                current_scope->define_function(qualified, sym);
            }
        }
    }
    bool creates_cycle = module_depends_on(module_id, current_module_id);
    if (creates_cycle) {
        if (!resolved_instances.count(instance.id)) {
            auto& pending = pending_imports[current_instance_id];
            if (std::find(pending.begin(), pending.end(), instance.id) == pending.end()) {
                pending.push_back(instance.id);
            }
        }
        return;
    }

    resolve_instance(instance.id);
}

bool Resolver::try_resolve_module_path(const std::vector<std::string>& import_path,
                                       const std::string& current_file,
                                       std::string& out_path) const {
    std::string relative = join_import_path(import_path) + ".vx";
    if (!try_resolve_relative_path(relative, current_file, project_root, out_path)) {
        return false;
    }
    out_path = std::filesystem::path(out_path).lexically_normal().string();
    return true;
}

void Resolver::build_module_imports() {
    module_imports.clear();
    for (const auto& mod_info : program.modules) {
        std::vector<std::vector<std::string>> imports;
        for (const auto& stmt : mod_info.module.top_level) {
            collect_imports(stmt, imports);
        }
        std::vector<int>& deps = module_imports[mod_info.id];
        for (const auto& import_path : imports) {
            std::string resolved;
            if (!try_resolve_module_path(import_path, mod_info.path, resolved)) {
                continue;
            }
            auto it = program.path_to_id.find(resolved);
            if (it != program.path_to_id.end()) {
                deps.push_back(it->second);
            }
        }
    }
}

void Resolver::collect_imports(StmtPtr stmt, std::vector<std::vector<std::string>>& out) const {
    if (!stmt) return;
    if (stmt->kind == Stmt::Kind::Import) {
        out.push_back(stmt->import_path);
    }
    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child) { collect_imports_expr(child, out); },
        [&](const StmtPtr& child) { collect_imports(child, out); });
}

void Resolver::collect_imports_expr(ExprPtr expr, std::vector<std::vector<std::string>>& out) const {
    if (!expr) return;
    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { collect_imports_expr(child, out); },
        [&](const StmtPtr& child) { collect_imports(child, out); });
}

bool Resolver::module_depends_on(int module_id, int target_module_id) const {
    if (module_id == target_module_id) return true;
    std::unordered_set<int> visited;
    std::vector<int> stack;
    stack.push_back(module_id);
    while (!stack.empty()) {
        int current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second) continue;
        auto it = module_imports.find(current);
        if (it == module_imports.end()) continue;
        for (int dep : it->second) {
            if (dep == target_module_id) return true;
            stack.push_back(dep);
        }
    }
    return false;
}

} // namespace vexel
