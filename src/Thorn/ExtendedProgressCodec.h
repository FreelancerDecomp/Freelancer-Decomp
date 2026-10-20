#pragma once
#include <cfenv>
#include <cstdint>
#include <cstring>
#include <limits>
#if defined(_WIN32) && defined(_M_IX86)
#include <float.h>
#endif

namespace ThornExtendedProgress
{
    static_assert(sizeof(float)==4 && sizeof(double)==8 &&
        std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
        "IEEE binary32/64 required");
    inline int Rounding()
    {
#if defined(_WIN32) && defined(_M_IX86)
        unsigned x87=0,sse=0;
        __control87_2(0,0,&x87,&sse);
        switch(x87&_MCW_RC) {
        case _RC_DOWN:return FE_DOWNWARD;
        case _RC_UP:return FE_UPWARD;
        case _RC_CHOP:return FE_TOWARDZERO;
        default:return FE_TONEAREST;
        }
#else
        return std::fegetround();
#endif
    }
    inline unsigned HighestBit(std::uint64_t value)
    {
        unsigned bit=0;
        while (value>>1) { value>>=1;++bit; }
        return bit;
    }
    inline std::uint64_t RoundRight(std::uint64_t value,unsigned shift,bool negative)
    {
        if (!shift) return value;
        const std::uint64_t whole=shift<64 ? value>>shift:0;
        const std::uint64_t remainder=shift<64 ? value&((UINT64_C(1)<<shift)-1):value;
        if (!remainder) return whole;
        const int mode=Rounding();
        bool increment=false;
        if (mode==FE_UPWARD) increment=!negative;
        else if (mode==FE_DOWNWARD) increment=negative;
        else if (mode==FE_TONEAREST && shift<=64) {
            const auto half=UINT64_C(1)<<(shift-1);
            increment=remainder>half || (remainder==half && (whole&1));
        }
        return whole+static_cast<unsigned>(increment);
    }
    inline void StoreDouble(unsigned char* output,double value)
    {
        std::uint64_t raw;
        std::memcpy(&raw,&value,sizeof(raw));
        const unsigned exponent=static_cast<unsigned>((raw>>52)&0x7ff);
        const auto fraction=raw&UINT64_C(0x000fffffffffffff);
        std::uint64_t significand=0;
        unsigned encodedExponent=0;
        if (exponent==0x7ff) {
            encodedExponent=0x7fff;
            significand=UINT64_C(0x8000000000000000)|(fraction<<11);
            if (fraction) significand|=UINT64_C(0x4000000000000000);
        } else if (exponent) {
            encodedExponent=exponent+(16383-1023);
            significand=UINT64_C(0x8000000000000000)|(fraction<<11);
        } else if (fraction) {
            const unsigned top=HighestBit(fraction);
            encodedExponent=top+(16383-1074);
            significand=fraction<<(63-top);
        }
        for (unsigned index=0;index<8;++index) output[index]=static_cast<unsigned char>(significand>>(index*8));
        output[8]=static_cast<unsigned char>(encodedExponent);
        output[9]=static_cast<unsigned char>((encodedExponent>>8)|((raw>>63)<<7));
    }
    inline std::uint64_t Pack(const unsigned char* input,unsigned digits,unsigned exponentBits,int minimum,int maximum,int bias)
    {
        std::uint64_t significand=0;
        for (unsigned index=0;index<8;++index) significand|=static_cast<std::uint64_t>(input[index])<<(index*8);
        const unsigned exponent=input[8]|((input[9]&0x7fu)<<8);
        const bool negative=(input[9]&0x80)!=0;
        const unsigned fractionBits=digits-1;
        const unsigned signShift=fractionBits+exponentBits;
        const std::uint64_t sign=static_cast<std::uint64_t>(negative)<<signShift;
        const std::uint64_t infinity=((UINT64_C(1)<<exponentBits)-1)<<fractionBits;
        const auto quiet=UINT64_C(1)<<(fractionBits-1);
        // An explicit integer bit is required for every nonzero normal exponent.
        if (exponent && !(significand>>63)) return (UINT64_C(1)<<signShift)|infinity|quiet;
        if (exponent==0x7fff) {
            const auto payload=significand&UINT64_C(0x7fffffffffffffff);
            return sign|infinity|(payload ? (payload>>(64-digits))|quiet:0);
        }
        if (!significand) return sign;
        const int power=static_cast<int>(exponent ? exponent:1)-16383-63;
        const unsigned top=HighestBit(significand);
        int resultExponent=static_cast<int>(top)+power;
        if (resultExponent<minimum) {
            const int shift=minimum-static_cast<int>(fractionBits)-power;
            const auto fraction=shift>0 ? RoundRight(significand,static_cast<unsigned>(shift),negative):
                significand<<static_cast<unsigned>(-shift);
            return sign|fraction; // A carry into bit fractionBits is the smallest normal.
        }
        const int shift=static_cast<int>(top)-static_cast<int>(fractionBits);
        auto rounded=shift>0 ? RoundRight(significand,static_cast<unsigned>(shift),negative):
            significand<<static_cast<unsigned>(-shift);
        if (rounded==(UINT64_C(1)<<digits)) { rounded>>=1;++resultExponent; }
        if (resultExponent>maximum) {
            const int mode=Rounding();
            const bool toInfinity=mode==FE_TONEAREST || (mode==FE_UPWARD && !negative) || (mode==FE_DOWNWARD && negative);
            return sign|(toInfinity ? infinity:infinity-1);
        }
        return sign|(static_cast<std::uint64_t>(resultExponent+bias)<<fractionBits)|
            (rounded&((UINT64_C(1)<<fractionBits)-1));
    }
    inline double LoadDouble(const unsigned char* input)
    {
        const auto bits=Pack(input,53,11,-1022,1023,1023);
        double result;std::memcpy(&result,&bits,sizeof(result));return result;
    }
    inline float LoadFloat(const unsigned char* input)
    {
        const auto bits=static_cast<std::uint32_t>(Pack(input,24,8,-126,127,127));
        float result;std::memcpy(&result,&bits,sizeof(result));return result;
    }
}
