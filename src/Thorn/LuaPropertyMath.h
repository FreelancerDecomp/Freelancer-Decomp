#pragma once
#include "PortableComparison.h"
#include <cfenv>
#include <cstdint>
#include <cstring>
#if defined(_WIN32) && defined(_M_IX86)
#include <float.h>
#endif

namespace ThornLuaPropertyMath
{
    inline unsigned Precision()
    {
#if defined(_WIN32) && defined(_M_IX86)
        unsigned control = 0;
        _controlfp_s(&control,0,0);
        return (control&_MCW_PC)==_PC_24 ? 24 : (control&_MCW_PC)==_PC_64 ? 64 : 53;
#else
        return 53;
#endif
    }
    inline int Rounding()
    {
#if defined(_WIN32) && defined(_M_IX86)
        unsigned x87 = 0, sse = 0;
        __control87_2(0,0,&x87,&sse);
        switch(x87&_MCW_RC) {
        case _RC_DOWN: return FE_DOWNWARD;
        case _RC_UP: return FE_UPWARD;
        case _RC_CHOP: return FE_TOWARDZERO;
        default: return FE_TONEAREST;
        }
#else
        return std::fegetround();
#endif
    }
    inline unsigned BitCount(std::uint64_t value)
    {
        unsigned count=0;
        for (; value; value>>=1) ++count;
        return count;
    }

    inline int Milliseconds(double number)
    {
        static_assert(sizeof(double)==8 && sizeof(int)==4 &&
            std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits==53,
            "IEEE binary64 and 32-bit integers required");
        std::uint64_t raw;
        std::memcpy(&raw,&number,sizeof(raw));
        const bool negative=(raw>>63)!=0;
        const unsigned exponent=static_cast<unsigned>((raw>>52)&0x7ff);
        if (exponent==0x7ff) { std::feraiseexcept(FE_INVALID); return 0; }
        std::uint64_t significand=(raw&UINT64_C(0x000fffffffffffff)) | (exponent ? UINT64_C(0x0010000000000000) : 0);
        if (!significand) return 0;
        int power=exponent ? static_cast<int>(exponent)-1075 : -1074;
        // A binary64 significand times 1000 fits in 63 bits. Retain that exact
        // product until the original precision-mode rounding and integer chop,
        // avoiding a binary64 spill that can change a millisecond by one.
        significand*=1000;
        const unsigned precision=Precision();
        unsigned bits=BitCount(significand);
        if (bits>precision) {
            const unsigned shift=bits-precision;
            const std::uint64_t remainder=significand&((UINT64_C(1)<<shift)-1);
            significand>>=shift;
            power+=shift;
            if (remainder) {
                const int rounding=Rounding();
                const auto half=UINT64_C(1)<<(shift-1);
                const bool increment=rounding==FE_TONEAREST ? remainder>half || (remainder==half && (significand&1)) :
                    rounding==FE_DOWNWARD ? negative : rounding==FE_UPWARD && !negative;
                if (increment) ++significand;
                std::feraiseexcept(FE_INEXACT);
            }
        }
        bits=BitCount(significand);
        if (static_cast<int>(bits)+power>64 ||
            (static_cast<int>(bits)+power==64 && (!negative || significand!=(UINT64_C(1)<<(bits-1))))) {
            std::feraiseexcept(FE_INVALID); return 0;
        }
        std::uint64_t integer;
        if (power>=0) integer=significand<<power;
        else {
            const unsigned shift=static_cast<unsigned>(-power);
            integer=shift>=64 ? 0 : significand>>shift;
            if (shift>=64 || (significand&((UINT64_C(1)<<shift)-1))) std::feraiseexcept(FE_INEXACT);
        }
        if (negative) integer=UINT64_C(0)-integer;
        const std::uint32_t low=static_cast<std::uint32_t>(integer);
        int result;
        std::memcpy(&result,&low,sizeof(result));
        return result;
    }

    template<class Props>
    inline void ApplyLight(Props& props, int& mask, int colorBit, int infiniteBit,
                           int cutoffBit, int typeBit, int diffuseBit)
    {
        const int entryMask=mask;
        if ((entryMask&colorBit)!=0) {
            const std::uint32_t scaleBits=0x3b808081u;
            float scale;
            std::memcpy(&scale,&scaleBits,sizeof(scale));
            const float red=static_cast<float>(static_cast<double>(props.Color[0])*scale);
            const float green=static_cast<float>(static_cast<double>(props.Color[1])*scale);
            const int blue=props.Color[2];
            props.Diffuse[0]=red; props.Diffuse[1]=green;
            mask=entryMask|diffuseBit;
            props.Diffuse[2]=static_cast<float>(static_cast<double>(blue)*scale);
            props.Diffuse[3]=1.0f;
        }
        const int infiniteMask=mask;
        if ((infiniteMask&infiniteBit)!=0) {
            props.Type=props.Infinite==0 ? 2 : 3;
            mask=(infiniteMask&~infiniteBit)|typeBit;
        }
        const int cutoffMask=mask;
        const float zero=0.0f;
        if ((cutoffMask&cutoffBit)!=0 && ThornPortableComparison::StrictLess(&props.Cutoff,&zero)) {
            props.Type=1;
            props.Cutoff=0.0f; props.Theta=0.0f;
            mask=cutoffMask|typeBit;
        }
    }
}
