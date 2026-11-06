#include "vx_runtime.h"

/* 0 sqrt, 1 floor, 2 ceil, 3 trunc, 4 round away, 5 min, 6 max,
 * 7 copysign, 8 isnan, 9 isinf, 10 isfinite, 11 signbit,
 * 12 integer square root, 13 round to nearest/even. */
static uint32_t math_root(uint32_t high, uint32_t low, uint32_t* remainder) VX_ABI {
    uint32_t root=0, trial, rest=0;
    uint8_t i;
    for(i=0;i<32;++i) {
        rest=(rest<<2)|(high>>30);
        high=(high<<2)|(low>>30);low<<=2;
        trial=(root<<2)|1;root<<=1;
        if(rest>=trial) {rest-=trial;root|=1;}
    }
    *remainder=rest;return root;
}
uint32_t vxi_math(uint32_t left,uint32_t right,uint8_t bits,uint8_t operation) VX_ABI {
    uint8_t fraction=bits==16 ? 10 : 23, negative;
    int16_t bias=bits==16 ? 15 : 127, exponent, result_exponent;
    uint32_t sign=1UL<<(bits-1), infinity=bits==16 ? 0x7c00UL : 0x7f800000UL;
    uint32_t nan=bits==16 ? 0x7e00UL : 0x7fc00000UL;
    uint32_t a=left&(sign-1), b=right&(sign-1), mantissa, high, low, root, remainder, unit;
    if(operation>=128) {
        uint32_t mask=0xffffffffUL>>(32-bits), integral;
        fraction=(uint8_t)right;operation-=128;
        negative=(right&256) && (left&sign);
        mantissa=negative ? (0UL-left)&mask : left&mask;
        if(fraction==32) {integral=0;remainder=mantissa;unit=0;}
        else {unit=1UL<<fraction;integral=mantissa>>fraction;remainder=mantissa&(unit-1);}
        high=fraction==32 ? 0x80000000UL : unit>>1;
        if(remainder && ((operation==1 && negative) || (operation==2 && !negative) ||
            (operation==4 && remainder>=high) ||
            (operation==13 && (remainder>high || (remainder==high && (integral&1)))))) ++integral;
        low=fraction==32 ? 0 : integral<<fraction;
        return (negative ? 0UL-low : low)&mask;
    }
    if(operation==12) {
        if(right && (left&sign)) {vx_trap(10);return 0;}
        return math_root(0,left,&remainder);
    }
    negative=(left&sign)!=0;
    if(operation==7) return a|(right&sign);
    if(operation==8) return a>infinity;
    if(operation==9) return a==infinity;
    if(operation==10) return a<infinity;
    if(operation==11) return negative;
    if(operation==5 || operation==6) {
        uint8_t less;
        if(a>infinity) return b>infinity ? nan : right;
        if(b>infinity) return left;
        if(!a && !b) return (operation==5 ? left|right : left&right)&sign;
        less=negative!=((right&sign)!=0) ? negative : negative ? a>b : a<b;
        return (operation==5 ? less : !less) ? left : right;
    }
    if(a>infinity) return nan;
    if(!a) return left;
    if(operation==0) {
        if(negative) return nan;
        if(a==infinity) return left;
        exponent=(int16_t)(a>>fraction);
        mantissa=a&((1UL<<fraction)-1);
        if(exponent) mantissa|=1UL<<fraction;
        else exponent=1;
        exponent-=bias;
        while(mantissa<(1UL<<fraction)) {mantissa<<=1;--exponent;}
        negative=(uint8_t)((uint16_t)exponent&1);
        result_exponent=(int16_t)((exponent-negative)/2);
        fraction+=negative;
        high=mantissa>>(32-fraction);low=mantissa<<fraction;
        root=math_root(high,low,&remainder);
        if(remainder>root) ++root;
        fraction=bits==16 ? 10 : 23;
        if(root==(1UL<<(fraction+1))) {root>>=1;++result_exponent;}
        return ((uint32_t)(result_exponent+bias)<<fraction)|(root&((1UL<<fraction)-1));
    }
    if(a==infinity) return left;
    exponent=(int16_t)(a>>fraction)-bias;
    if(exponent>=fraction) return left;
    if(exponent<0) {
        uint32_t half=(uint32_t)(bias-1)<<fraction;
        uint8_t one=(operation==1 && negative) || (operation==2 && !negative) ||
            (operation==4 && a>=half) || (operation==13 && a>half);
        return (left&sign)|(one ? (uint32_t)bias<<fraction : 0);
    }
    unit=1UL<<(fraction-exponent);
    remainder=a&(unit-1);mantissa=a&~(unit-1);
    if(remainder && ((operation==1 && negative) || (operation==2 && !negative) ||
        (operation==4 && remainder>=(unit>>1)) ||
        (operation==13 && (remainder>(unit>>1) || (remainder==(unit>>1) && (mantissa&unit)))))) mantissa+=unit;
    return (left&sign)|mantissa;
}
