#pragma once
#include "vexel.hpp"

namespace vx {
struct FloatParts {
    Big numerator = 0, denominator = 1; // Unsigned magnitude, including zero.
    bool negative = false, infinity = false, nan = false;
};
FloatParts splitFloat(const Big& raw,unsigned width);
Big joinFloat(const FloatParts& value,unsigned width);
Big floatLiteral(const std::string& text,unsigned maxBits,Span span);
Big floatUnary(const std::string& op,const Big& raw,unsigned width);
Big floatMath(unsigned operation,const Big& left,const Big& right,unsigned width);
unsigned mathOperation(const std::string& name);
Big floatBinary(const std::string& op,const Big& left,const Big& right,unsigned width);
std::optional<int> floatCompare(const Big& left,const Big& right,unsigned width);
std::string formatFloat(const Big& raw,unsigned width);
}
