#include "floating.hpp"
#include <algorithm>
#include <bit>
#include <charconv>

namespace vx {
namespace {
struct Format { unsigned fraction, exponent; int bias; };
Format format(unsigned bits) {
    if (bits == 16) return {10,5,15};
    if (bits == 32) return {23,8,127};
    if (bits == 64) return {52,11,1023};
    throw std::logic_error("unsupported floating-point width");
}
Big rounded(const Big& numerator,const Big& denominator) {
    Big quotient = numerator/denominator;
    Big twice = numerator%denominator; twice <<= 1;
    if (twice > denominator || (twice == denominator && (quotient&1) != 0)) ++quotient;
    return quotient;
}
FloatParts special(bool negative,bool infinity,bool nan) {
    FloatParts result; result.negative=negative; result.infinity=infinity; result.nan=nan; return result;
}
}
FloatParts splitFloat(const Big& raw,unsigned width) {
    const auto f = format(width);
    FloatParts result;
    result.negative = (raw & (Big(1) << (width-1))) != 0;
    unsigned exponent = ((raw >> f.fraction)&((Big(1)<<f.exponent)-1)).convert_to<unsigned>();
    result.numerator = raw & ((Big(1)<<f.fraction)-1);
    if (exponent == (1U<<f.exponent)-1) {
        result.nan = result.numerator != 0; result.infinity = !result.nan; return result;
    }
    int power = 1-f.bias-static_cast<int>(f.fraction);
    if (exponent) {
        result.numerator |= Big(1)<<f.fraction;
        power = static_cast<int>(exponent)-f.bias-static_cast<int>(f.fraction);
    }
    if (power >= 0) result.numerator <<= static_cast<unsigned>(power);
    else result.denominator <<= static_cast<unsigned>(-power);
    return result;
}
Big joinFloat(const FloatParts& value,unsigned width) {
    const auto f = format(width);
    Big sign = value.negative ? Big(1)<<(width-1) : Big(0);
    Big infinity = ((Big(1)<<f.exponent)-1)<<f.fraction;
    if (value.nan) return infinity | (Big(1)<<(f.fraction-1)); // Canonical positive quiet NaN.
    if (value.infinity) return sign | infinity;
    if (value.numerator == 0) return sign;
    if (value.numerator < 0 || value.denominator <= 0) throw std::logic_error("invalid floating-point ratio");
    int exponent = static_cast<int>(boost::multiprecision::msb(value.numerator))-
                   static_cast<int>(boost::multiprecision::msb(value.denominator));
    if (exponent >= 0 ? value.numerator < (value.denominator << static_cast<unsigned>(exponent)) :
        (value.numerator << static_cast<unsigned>(-exponent)) < value.denominator) --exponent;
    if (exponent > f.bias) return sign | infinity;
    const int unit = std::max(exponent,1-f.bias)-static_cast<int>(f.fraction);
    Big numerator=value.numerator, denominator=value.denominator;
    if (unit < 0) numerator <<= static_cast<unsigned>(-unit);
    else denominator <<= static_cast<unsigned>(unit);
    Big significand = rounded(numerator,denominator);
    if (significand == 0) return sign;
    if (significand < (Big(1)<<f.fraction)) return sign | significand; // Gradual underflow.
    exponent = std::max(exponent,1-f.bias);
    if (significand == (Big(1)<<(f.fraction+1))) { significand >>= 1; ++exponent; }
    if (exponent > f.bias) return sign | infinity;
    return sign | (Big(exponent+f.bias)<<f.fraction) | (significand-(Big(1)<<f.fraction));
}
Big floatLiteral(const std::string& text,unsigned maxBits,Span span) {
    Big magnitude=0;
    int fraction=0, exponent=0;
    bool afterDot=false, exponentNegative=false;
    std::size_t i=0;
    for (;i<text.size() && text[i]!='e' && text[i]!='E';++i) {
        if (text[i]=='.') { afterDot=true; continue; }
        if (text[i]<'0' || text[i]>'9') throw Error(span,"invalid floating-point literal");
        magnitude*=10; magnitude+=text[i]-'0';
        if (magnitude!=0 && boost::multiprecision::msb(magnitude)>=maxBits) throw Error(span,"floating literal exceeds solver bit budget",Diagnostic::Exhausted);
        if (afterDot) ++fraction;
    }
    if (i<text.size()) {
        ++i;
        if (i<text.size() && (text[i]=='+' || text[i]=='-')) exponentNegative=text[i++]=='-';
        if (i==text.size()) throw Error(span,"floating exponent requires decimal digits");
        for (;i<text.size();++i) {
            if (text[i]<'0' || text[i]>'9') throw Error(span,"invalid floating exponent");
            exponent=std::min(100000,exponent*10+(text[i]-'0'));
        }
    }
    if (magnitude==0) return 0;
    int power=(exponentNegative ? -exponent : exponent)-fraction;
    if (power>4096) throw Error(span,"floating literal exceeds #f64 range");
    if (power < -4096) return 0;
    FloatParts value; value.numerator=magnitude;
    Big scale=1;
    for (int j=0;j<std::abs(power);++j) scale*=10;
    if (power>=0) value.numerator*=scale; else value.denominator=scale;
    Big bits=joinFloat(value,64);
    if (splitFloat(bits,64).infinity) throw Error(span,"floating literal exceeds #f64 range");
    return bits;
}
Big floatUnary(const std::string& op,const Big& raw,unsigned width) {
    Big sign=Big(1)<<(width-1);
    if (op=="+") return raw;
    if (op=="-") return raw^sign;
    if (op=="|") return raw & (sign-1);
    throw std::logic_error("invalid floating unary operation");
}
Big floatBinary(const std::string& op,const Big& left,const Big& right,unsigned width) {
    FloatParts a=splitFloat(left,width), b=splitFloat(right,width);
    auto result = [&](FloatParts value) { return joinFloat(value,width); };
    auto nan = [&]() { return result(special(false,false,true)); };
    if (a.nan || b.nan) return nan();
    if (op=="+" || op=="-") {
        if (op=="-") b.negative=!b.negative;
        if (a.infinity || b.infinity) {
            if (a.infinity && b.infinity && a.negative!=b.negative) return nan();
            return result(a.infinity ? a : b);
        }
        Big x=a.numerator*b.denominator, y=b.numerator*a.denominator;
        if (a.negative) x=-x;
        if (b.negative) y=-y;
        FloatParts sum; sum.numerator=x+y; sum.denominator=a.denominator*b.denominator;
        sum.negative=sum.numerator<0 || (sum.numerator==0 && a.negative && b.negative);
        if (sum.numerator<0) sum.numerator=-sum.numerator;
        return result(sum);
    }
    bool negative=a.negative!=b.negative;
    FloatParts value; value.negative=negative;
    if (op=="*") {
        if ((a.infinity && !b.infinity && b.numerator==0) || (b.infinity && !a.infinity && a.numerator==0)) return nan();
        if (a.infinity || b.infinity) return result(special(negative,true,false));
        value.numerator=a.numerator*b.numerator; value.denominator=a.denominator*b.denominator;
    } else if (op=="/") {
        if ((a.infinity && b.infinity) || (!a.infinity && a.numerator==0 && !b.infinity && b.numerator==0)) return nan();
        if (a.infinity || (!b.infinity && b.numerator==0)) return result(special(negative,true,false));
        if (b.infinity) return result(value);
        value.numerator=a.numerator*b.denominator; value.denominator=a.denominator*b.numerator;
    } else if (op=="%") {
        if (a.infinity || (!b.infinity && b.numerator==0)) return nan();
        if (b.infinity) return result(a);
        Big quotient=(a.numerator*b.denominator)/(a.denominator*b.numerator);
        value.numerator=a.numerator*b.denominator-quotient*b.numerator*a.denominator;
        value.denominator=a.denominator*b.denominator; value.negative=a.negative;
    } else throw std::logic_error("invalid floating binary operation");
    return result(value);
}
std::optional<int> floatCompare(const Big& left,const Big& right,unsigned width) {
    FloatParts a=splitFloat(left,width), b=splitFloat(right,width);
    if (a.nan || b.nan) return {};
    if (!a.infinity && !b.infinity && a.numerator==0 && b.numerator==0) return 0;
    if (a.negative!=b.negative) return a.negative ? -1 : 1;
    int order;
    if (a.infinity || b.infinity) order=a.infinity==b.infinity ? 0 : a.infinity ? 1 : -1;
    else {
        Big x=a.numerator*b.denominator, y=b.numerator*a.denominator;
        order=x<y ? -1 : x>y ? 1 : 0;
    }
    return a.negative ? -order : order;
}
std::string formatFloat(const Big& raw,unsigned width) {
    auto value=splitFloat(raw,width);
    if (value.nan) return "nan";
    if (value.infinity) return value.negative ? "-inf" : "inf";
    char buffer[128];
    std::to_chars_result out;
    if (width==64) out=std::to_chars(buffer,buffer+sizeof buffer,std::bit_cast<double>(raw.convert_to<std::uint64_t>()));
    else {
        Big bits=width==32 ? raw : joinFloat(value,32);
        out=std::to_chars(buffer,buffer+sizeof buffer,std::bit_cast<float>(bits.convert_to<std::uint32_t>()));
    }
    if (out.ec!=std::errc{}) throw std::logic_error("floating formatting failed");
    return {buffer,out.ptr};
}
}
