#pragma once
#include "machine.hpp"
#include <optional>

namespace vx::relational {
// A conservative ordered interval of integer values. Wrapping images which
// cross a representation boundary retain the full target domain.
struct IntegerRange {Integer lower,upper;bool operator==(const IntegerRange&) const = default;};
using IntegerBound=std::optional<IntegerRange>;
IntegerRange integerDomain(unsigned bits,bool sign);
IntegerBound convertedIntegerRange(IntegerBound value,unsigned bits,bool sign);
IntegerBound arithmeticIntegerRange(const std::string& operation,IntegerBound left,IntegerBound right,
                                    unsigned bits,bool sign);
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
