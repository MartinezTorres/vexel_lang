#include "floating.hpp"
#include <algorithm>
#include <boost/multiprecision/integer.hpp>

namespace vx {
unsigned mathOperation(const std::string& name) {
    static const std::vector<std::string> names={"sqrt","floor","ceil","trunc","round","min","max",
        "copysign","isnan","isinf","isfinite","signbit","isqrt","roundeven","abs","fmod","pow"};
    auto found=std::find(names.begin(),names.end(),name);
    if(found==names.end()) throw std::logic_error("unknown bundled math operation: "+name);
    return static_cast<unsigned>(found-names.begin());
}
Big floatMath(unsigned op,const Big& left,const Big& right,unsigned bits) {
    unsigned fraction=bits==16 ? 10 : bits==32 ? 23 : 52;
    int bias=bits==16 ? 15 : bits==32 ? 127 : 1023;
    Big sign=Big(1)<<(bits-1), infinity=((Big(1)<<(bits-fraction-1))-1)<<fraction;
    Big nan=infinity|(Big(1)<<(fraction-1)), a=left&(sign-1), b=right&(sign-1);
    bool negative=(left&sign)!=0;
    if(op==7) return a|(right&sign);
    if(op==8) return a>infinity ? 1 : 0;
    if(op==9) return a==infinity ? 1 : 0;
    if(op==10) return a<infinity ? 1 : 0;
    if(op==11) return negative ? 1 : 0;
    if(op==5 || op==6) {
        if(a>infinity) return b>infinity ? nan : right;
        if(b>infinity) return left;
        if(a==0 && b==0) return (op==5 ? Big(left|right) : Big(left&right))&sign;
        int order=*floatCompare(left,right,bits);
        return (op==5 ? order<0 : order>=0) ? left : right;
    }
    if(a>infinity) return nan;
    if(a==0) return left;
    int exponent=(a>>fraction).convert_to<int>();
    if(op==0) {
        if(negative) return nan;
        if(a==infinity) return left;
        Big mantissa=a&((Big(1)<<fraction)-1);
        if(exponent) mantissa|=Big(1)<<fraction;
        else exponent=1;
        exponent-=bias;
        while(mantissa<(Big(1)<<fraction)) {mantissa<<=1;--exponent;}
        int resultExponent=(exponent-(exponent%2!=0))/2;
        Big radicand=mantissa<<(fraction+(exponent%2!=0)), remainder;
        Big root=boost::multiprecision::sqrt(radicand,remainder);
        if(remainder>root) ++root;
        if(root==(Big(1)<<(fraction+1))) {root>>=1;++resultExponent;}
        return (Big(resultExponent+bias)<<fraction)|(root&((Big(1)<<fraction)-1));
    }
    if(a==infinity) return left;
    exponent-=bias;
    if(exponent>=static_cast<int>(fraction)) return left;
    if(exponent<0) {
        Big half=Big(bias-1)<<fraction;
        bool one=(op==1 && negative) || (op==2 && !negative) || (op==4 && a>=half) || (op==13 && a>half);
        return (left&sign)|(one ? Big(Big(bias)<<fraction) : Big(0));
    }
    Big unit=Big(1)<<(fraction-static_cast<unsigned>(exponent)), remainder=a&(unit-1), integral=a&~(unit-1);
    if(remainder!=0 && ((op==1 && negative) || (op==2 && !negative) || (op==4 && remainder>=(unit>>1)) ||
        (op==13 && (remainder>(unit>>1) || (remainder==(unit>>1) && (integral&unit)!=0))))) integral+=unit;
    return (left&sign)|integral;
}
}
