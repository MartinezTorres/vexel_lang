#include "resolver.h"
#include "typechecker.h"

namespace vexel {

Resolver::Resolver(TypeChecker* checker)
    : checker(checker) {}

void Resolver::predeclare(Module& mod) {
    if (!checker) return;

    checker->validate_annotations(mod);

    // Pass 1: declare functions and types (no bodies/initializers).
    for (auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            std::string func_name = stmt->func_name;
            if (!stmt->type_namespace.empty()) {
                func_name = stmt->type_namespace + "::" + stmt->func_name;
            }

            stmt->is_generic = checker->is_generic_function(stmt);
            checker->verify_no_shadowing(func_name, stmt->location);

            Symbol sym;
            sym.kind = Symbol::Kind::Function;
            sym.is_external = stmt->is_external;
            sym.is_exported = stmt->is_exported;
            sym.declaration = stmt;
            checker->current_scope->define(func_name, sym);
        } else if (stmt->kind == Stmt::Kind::TypeDecl) {
            checker->verify_no_shadowing(stmt->type_decl_name, stmt->location);

            Symbol sym;
            sym.kind = Symbol::Kind::Type;
            sym.declaration = stmt;
            checker->current_scope->define(stmt->type_decl_name, sym);
        }
        // Constants are intentionally not pre-declared.
    }
}

} // namespace vexel
