#pragma once
#include "ast.h"

namespace vexel {

class TypeChecker;

class Resolver {
public:
    explicit Resolver(TypeChecker* checker);
    void predeclare(Module& mod);

private:
    TypeChecker* checker;
};

} // namespace vexel
