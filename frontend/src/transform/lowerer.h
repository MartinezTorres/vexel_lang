#pragma once
#include "ast.h"

namespace vexel {

class TypeChecker;

// Lowerer normalizes typed AST shape for backend consumption.
// Canonical lowered contract is defined by this pass:
// - no generics/expression parameters in emitted lowered module
// - iteration/repeat bodies normalized to blocks
// - statement-only forms keep null value type
class Lowerer {
public:
    explicit Lowerer(TypeChecker* checker);
    void run(Module& mod);

private:
    TypeChecker* checker;

    TypePtr lower_type(TypePtr type);
    void lower_stmt(StmtPtr stmt);
    ExprPtr lower_expr(ExprPtr expr);
};

} // namespace vexel
