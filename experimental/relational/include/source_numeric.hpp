#pragma once
#include "machine.hpp"
#include <optional>

namespace vx::relational {
// Language adapter formats; none of these tags are part of the search kernel.
struct ScalarFormat {
    enum Kind { Exact, IntegerKind, Fixed, Float, Bool } kind=Exact;
    unsigned bits=0,fraction=0;
    bool sign=false;
};
std::optional<ScalarFormat> scalarFormat(const Ref& type);
Ref extraNumericType(Terms& terms,const std::string& spelling);
unsigned scalarBits(const Ref& type);
bool scalarSigned(const Ref& type);
bool nativeScalar(const Ref& type);
void numericRules(Program& program,const std::vector<Ref>& formats);
}
