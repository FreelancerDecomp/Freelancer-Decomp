// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_message_router.h"

#include <cstdarg>
#include <cstdio>

static_assert(sizeof(void*) != 4 || sizeof(IVP_Message_Router) == 0x2008,
    "IVP_Message_Router: retail instance spans 0658c730..0658e738");

IVP_Message_Router G_IVPMessageRouter;

// 063922c0: vtable 063a5ac4, REP STOSD of 0x800 zero dwords at +8, then
// [+4] = 1.
IVP_Message_Router::IVP_Message_Router()
{
    for (int32_t Index = 0; Index < 0x800; ++Index)
    {
        CategoryLevels[Index] = 0;
    }
    bEnabled = 1;
}

// 063922e0 -> 06392300: restores the vtable only; the deleting variant frees
// through FUN_06391d5a (operator delete).
IVP_Message_Router::~IVP_Message_Router()
{
}

// 063922a0: PUSH [ESP+8] (Text); PUSH "%s"; CALL printf; RET 8.
void IVP_Message_Router::Print(int32_t Category, const char* Text)
{
    (void)Category;
    printf("%s", Text);
}

// 06392220.
int32_t IVP_Message_Router::IsCategoryEnabled(int32_t Category) const
{
    if (Category >= 0x800)
    {
        return 0;
    }
    if (CategoryLevels[Category] == 0)
    {
        return 0;
    }
    if (bEnabled == 0)
    {
        return 0;
    }
    return 1;
}

// 06392250: _chkstk(0x1000); vsprintf(Buffer, Format, &Format + 1);
// this->vtable[0](Category, Buffer).
void IVP_Message_Router::Report(int32_t Category, const char* Format, ...)
{
    char Buffer[0x1000];
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);
    Print(Category, Buffer);
}
