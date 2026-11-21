#include "source_numeric.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace vx::relational;
namespace {
int byte(int value,bool sign) {
    value=(value%256+256)%256;
    return sign && value>=128 ? value-256 : value;
}
void require(bool answer,const char* message) {if(!answer) throw std::runtime_error(message);}
}
int main() {
    // Enumerate the concrete byte values inside representative intervals,
    // independently of the abstract transfer implementation. Include wrap,
    // sign boundaries, zero divisors and shift counts wider than the value.
    for(bool sign:{false,true}) {
        const std::vector<std::pair<int,int>> ranges=sign ?
            std::vector<std::pair<int,int>>{{-128,-128},{-128,-1},{-9,-2},{-3,4},{0,0},{1,127},{-128,127}} :
            std::vector<std::pair<int,int>>{{0,0},{1,7},{2,255},{127,128},{254,255},{0,255}};
        auto counts=ranges;
        for(auto range:{std::pair{1,2},std::pair{0,7},std::pair{7,9},std::pair{255,256}}) counts.push_back(range);
        for(auto [alo,ahi]:ranges) for(auto [blo,bhi]:counts) for(const std::string op:{"/","%","<<",">>"}) {
            const auto bound=arithmeticIntegerRange(op,IntegerRange{alo,ahi},IntegerRange{blo,bhi},8,sign);
            require(bound.has_value(),"missing byte interval");
            for(int rawA=alo;rawA<=ahi;++rawA) for(int rawB=blo;rawB<=bhi;++rawB) {
                const int a=byte(rawA,sign),b=byte(rawB,sign);int actual;
                if(op=="/" || op=="%") {
                    if(!b) continue;
                    actual=op=="/" ? a/b : a%b;
                } else {
                    const auto count=static_cast<std::uint32_t>(rawB);
                    if(count>=8) continue;
                    const int scale=1<<count;
                    if(op=="<<") actual=a*scale;
                    else actual=a>=0 ? a/scale : -((-a+scale-1)/scale);
                }
                actual=byte(actual,sign);
                if(actual<bound->lower || actual>bound->upper)
                    throw std::runtime_error("unsound "+op+" interval at "+std::to_string(rawA)+","+std::to_string(rawB));
            }
        }
    }
    require(arithmeticIntegerRange("/",IntegerRange{1,7},IntegerRange{2,3},8,false)==IntegerBound(IntegerRange{0,3}),"positive quotient envelope");
    require(arithmeticIntegerRange("%",IntegerRange{-9,-2},IntegerRange{-5,-3},8,true)==IntegerBound(IntegerRange{-4,0}),"signed remainder envelope");
    require(arithmeticIntegerRange(">>",IntegerRange{-128,-1},IntegerRange{1,2},8,true)==IntegerBound(IntegerRange{-64,-1}),"arithmetic shift envelope");
    require(arithmeticIntegerRange("/",IntegerRange{-128,-128},IntegerRange{-1,-1},8,true)==IntegerBound(IntegerRange{-128,-128}),"minimum/-1 wrapping");
    require(arithmeticIntegerRange("/",IntegerRange{0,0xffffffffu},IntegerRange{65536,65536},32,false)==IntegerBound(IntegerRange{0,65535}),"dword quotient envelope");
    require(arithmeticIntegerRange("<<",IntegerRange{1,1},IntegerRange{256,256},8,false)==IntegerBound(integerDomain(8,false)),"shift count was truncated to byte width");
    std::cout<<"Integer domains: concrete division, remainder and shifts stay inside shared bounds\n";
}
