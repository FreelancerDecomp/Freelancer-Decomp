#pragma once
#include <cfenv>
#if defined(_WIN32) && defined(_M_IX86)
#include <float.h>
#endif
namespace ThornFpEnvironment
{
    inline unsigned Precision()
    {
#if defined(_WIN32) && defined(_M_IX86)
        unsigned control=0;_controlfp_s(&control,0,0);
        return (control&_MCW_PC)==_PC_24 ? 24:(control&_MCW_PC)==_PC_64 ? 64:53;
#else
        return 53;
#endif
    }
    inline int Rounding()
    {
#if defined(_WIN32) && defined(_M_IX86)
        unsigned x87=0,sse=0;__control87_2(0,0,&x87,&sse);
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
}
