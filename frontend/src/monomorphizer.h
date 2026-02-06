#pragma once
#include "ast.h"

namespace vexel {

class TypeChecker;

class Monomorphizer {
public:
    explicit Monomorphizer(TypeChecker* checker);
    void run(Module& mod);

private:
    TypeChecker* checker;
};

} // namespace vexel
