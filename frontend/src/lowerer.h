#pragma once
#include "ast.h"

namespace vexel {

class TypeChecker;

class Lowerer {
public:
    explicit Lowerer(TypeChecker* checker);
    void run(Module& mod);

private:
    TypeChecker* checker;

    void lower_stmt(StmtPtr stmt);
    ExprPtr lower_expr(ExprPtr expr);
};

} // namespace vexel
