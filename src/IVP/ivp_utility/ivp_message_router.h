// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

// IVP diagnostic message router (Common.dll, retail class name not in the
// binary: the IVP code is built without RTTI, vtable 063a5ac4 has no COL).
//
// Layout (0x2008 bytes, the static instance DAT_0658c730 ends at 0658e738):
//   +0x00 vtable 063a5ac4 {063922a0 Print, 063922e0 scalar deleting dtor}
//   +0x04 bEnabled          (ctor 063922c0 stores 1)
//   +0x08 CategoryLevels[0x800] (ctor 063922c0 zeroes them with REP STOSD)
//
// Nothing in retail writes a category level: DAT_0658c730 has exactly ten
// XREFs (the init / atexit pair 06392320 / 06392340 and four
// IsCategoryEnabled + Report pairs at 0636a05b, 0636c436, 0636c734, 0636c758)
// and no instruction addresses 0658c738..0658e737. IsCategoryEnabled is
// therefore always false in retail and no message is ever printed; the call
// sites are kept anyway because that is what retail executes.
class IVP_Message_Router
{
public:
    // 063922c0 (reached from the static-init entry 06392310 -> 06392320).
    IVP_Message_Router();

    // Slot 0, 063922a0: printf("%s", Text) (format DAT_0639c454); the
    // category is not read. RET 8. Declared before the dtor so that it
    // takes slot 0, as in retail.
    virtual void Print(int32_t Category, const char* Text);

    // Slot 1, 063922e0: scalar deleting dtor around 06392300, which only
    // restores the vtable.
    virtual ~IVP_Message_Router();

    // 06392220: Category < 0x800 (signed, JGE) && CategoryLevels[Category]
    // != 0 && bEnabled != 0. RET 4.
    int32_t IsCategoryEnabled(int32_t Category) const;

    // 06392250: vsprintf into a 0x1000-byte stack buffer, then slot 0
    // (Category, Buffer). Retail is a cdecl variadic with the router as the
    // first stack argument -- the MSVC variadic member convention.
    void Report(int32_t Category, const char* Format, ...);

    int32_t bEnabled;                  // +0x04
    int32_t CategoryLevels[0x800];     // +0x08
};

// DAT_0658c730: constructed by the CRT static-init entry 06392310 and
// destroyed through the atexit thunk 06392340 (both compiler generated for
// this global).
extern IVP_Message_Router G_IVPMessageRouter;
