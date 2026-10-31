#pragma once
#include "FpEnvironment.h"
#include <cstdint>
#include <cstring>
#include <limits>

namespace ThornSceneInteger
{
    // Fixed-point workspace at quantum 2^-1074. Only products below 2^64
    // can survive the final signed-qword conversion, so 20 limbs cover the
    // full binary64 fraction range plus cancellation against an int32 base.
    struct Magnitude { std::uint64_t word[20]={}; };
    static constexpr unsigned Quantum=1074;
    inline int Top(const Magnitude& value)
    {
        for (int index=19;index>=0;--index) if (value.word[index]) {
            unsigned bit=0;
            std::uint32_t part=static_cast<std::uint32_t>(value.word[index]>>32);
            if (part) bit=32;
            else part=static_cast<std::uint32_t>(value.word[index]);
            while (part>1) {part>>=1;++bit;}
            return index*64+static_cast<int>(bit);
        }
        return -1;
    }
    inline void Insert(Magnitude& value,std::uint64_t bits,unsigned shift)
    {
        const unsigned index=shift/64,offset=shift%64;
        value.word[index]|=bits<<offset;
        if (offset && index+1<20) value.word[index+1]|=bits>>(64-offset);
    }
    inline std::uint64_t Extract(const Magnitude& value,unsigned shift)
    {
        const unsigned index=shift/64,offset=shift%64;
        return (value.word[index]>>offset)|
            (offset && index+1<20 ? value.word[index+1]<<(64-offset):0);
    }
    inline bool Below(const Magnitude& value,unsigned limit)
    {
        for (unsigned index=0;index<limit/64;++index) if (value.word[index]) return true;
        return limit%64 && (value.word[limit/64]&((UINT64_C(1)<<(limit%64))-1));
    }
    inline int Compare(const Magnitude& a,const Magnitude& b)
    {
        for (int index=19;index>=0;--index) if (a.word[index]!=b.word[index]) return a.word[index]>b.word[index] ? 1:-1;
        return 0;
    }
    inline Magnitude Add(const Magnitude& a,const Magnitude& b)
    {
        Magnitude result;std::uint64_t carry=0;
        for (unsigned index=0;index<20;++index) {
            const auto partial=a.word[index]+b.word[index];
            result.word[index]=partial+carry;
            carry=(partial<a.word[index]) || (result.word[index]<partial);
        }
        return result;
    }
    inline Magnitude Subtract(const Magnitude& a,const Magnitude& b)
    {
        Magnitude result;std::uint64_t borrow=0;
        for (unsigned index=0;index<20;++index) {
            const auto partial=a.word[index]-b.word[index];
            result.word[index]=partial-borrow;
            borrow=(a.word[index]<b.word[index]) || (partial<borrow);
        }
        return result;
    }
    inline void Round(Magnitude& value,unsigned precision,bool negative)
    {
        const int shift=Top(value)+1-static_cast<int>(precision);
        if (shift<=0 || !Below(value,static_cast<unsigned>(shift))) return;
        const bool guard=(value.word[(shift-1)/64]>>((shift-1)%64))&1;
        const bool sticky=Below(value,static_cast<unsigned>(shift-1));
        auto high=Extract(value,static_cast<unsigned>(shift));
        const int mode=ThornFpEnvironment::Rounding();
        const bool increment=mode==FE_TONEAREST ? guard && (sticky || (high&1)) :
            mode==FE_UPWARD ? !negative:mode==FE_DOWNWARD && negative;
        unsigned position=static_cast<unsigned>(shift);
        if (increment) {
            if (high==UINT64_MAX) {high=UINT64_C(1)<<63;++position;}
            else ++high;
        }
        value=Magnitude{};Insert(value,high,position);
        std::feraiseexcept(FE_INEXACT);
    }
    inline int Invalid() {std::feraiseexcept(FE_INVALID);return 0;}
    inline int Interpolate(int from,std::uint32_t difference,bool signedDifference,
                           std::uint64_t significand,int power,bool negative,bool exceptional)
    {
        if (exceptional) return Invalid();
        std::uint32_t multiplier=difference;
        if (signedDifference && (difference&0x80000000u)) {multiplier=0u-difference;negative=!negative;}
        const auto lowProduct=(significand&0xffffffffu)*multiplier;
        const auto highProduct=(significand>>32)*multiplier;
        const auto low=lowProduct+(highProduct<<32);
        const auto high=(highProduct>>32)+static_cast<unsigned>(low<lowProduct);
        unsigned productBits=0;
        if (high) {for (auto v=high;v;v>>=1) ++productBits;productBits+=64;}
        else for (auto v=low;v;v>>=1) ++productBits;
        if (productBits && static_cast<int>(productBits)+power>64) return Invalid();
        Magnitude product;
        if (productBits) {
            Insert(product,low,static_cast<unsigned>(power+static_cast<int>(Quantum)));
            if (high) Insert(product,high,static_cast<unsigned>(power+static_cast<int>(Quantum)+64));
        }
        const unsigned precision=ThornFpEnvironment::Precision();
        Round(product,precision,negative);
        Magnitude base;
        const auto fromBits=static_cast<std::uint32_t>(from);
        const bool baseNegative=from<0;
        Insert(base,baseNegative ? 0u-fromBits:fromBits,Quantum);
        Magnitude result;
        if (negative==baseNegative) result=Add(product,base);
        else if (Compare(product,base)>=0) result=Subtract(product,base);
        else {result=Subtract(base,product);negative=baseNegative;}
        Round(result,precision,negative);
        if (Top(result)>=static_cast<int>(Quantum+64)) return Invalid();
        const auto integer=Extract(result,Quantum);
        if ((!negative && integer>UINT64_C(0x7fffffffffffffff)) ||
            (negative && integer>UINT64_C(0x8000000000000000))) return Invalid();
        if (Below(result,Quantum)) std::feraiseexcept(FE_INEXACT);
        const auto bits=static_cast<std::uint32_t>(negative ? UINT64_C(0)-integer:integer);
        int output;std::memcpy(&output,&bits,sizeof(output));return output;
    }
    inline int Color(int from,int to,const float* progress)
    {
        static_assert(sizeof(int)==4 && sizeof(float)==4,"32-bit scene fields required");
        std::uint32_t raw;std::memcpy(&raw,progress,4);
        const unsigned exponent=(raw>>23)&0xff;
        const std::uint64_t significand=(raw&0x7fffffu)|(exponent ? 0x800000u:0);
        return Interpolate(from,static_cast<std::uint32_t>(to)-static_cast<std::uint32_t>(from),true,
            significand,exponent ? static_cast<int>(exponent)-150:-149,(raw>>31)!=0,exponent==0xff);
    }
    inline int Audio(int from,int to,double progress)
    {
        static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559,"IEEE binary64 required");
        std::uint64_t raw;std::memcpy(&raw,&progress,8);
        const unsigned exponent=static_cast<unsigned>((raw>>52)&0x7ff);
        const auto significand=(raw&UINT64_C(0x000fffffffffffff))|(exponent ? UINT64_C(0x0010000000000000):0);
        return Interpolate(from,static_cast<std::uint32_t>(to)-static_cast<std::uint32_t>(from),false,
            significand,exponent ? static_cast<int>(exponent)-1075:-1074,(raw>>63)!=0,exponent==0x7ff);
    }
}
