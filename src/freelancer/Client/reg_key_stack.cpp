// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "reg_key_stack.h"

#include <string.h>

// DAT_006143b0 (reg_key_stack.h).
char const* G_FreelancerRegKey = "Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0";

FRegKeyStack::FRegKeyStack()
    : Depth(0)
{
    CloseAll();
    Keys[Depth] = HKEY_CURRENT_USER;
    ++Depth;
}

FRegKeyStack::~FRegKeyStack()
{
    CloseAll();
}

// FUN_005687c0.
HKEY FRegKeyStack::Pop()
{
    if (Depth > 0)
    {
        --Depth;
        return Keys[Depth];
    }
    return nullptr;
}

// FUN_005687e0.
void FRegKeyStack::CloseAll()
{
    while (Depth > 0)
    {
        --Depth;
        RegCloseKey(Keys[Depth]);
    }
}

// FUN_00568810.
void FRegKeyStack::PopClose()
{
    if (Depth > 0)
    {
        --Depth;
        RegCloseKey(Keys[Depth]);
    }
}

// FUN_00568830.
HKEY FRegKeyStack::Open(char const* SubKey, REGSAM Access)
{
    if (Depth <= 0 || Keys[Depth - 1] == nullptr || Depth >= 32)
        return nullptr;
    HKEY Key = nullptr;
    if (RegOpenKeyExA(Keys[Depth - 1], SubKey, 0, Access, &Key) != ERROR_SUCCESS)
        return nullptr;
    Keys[Depth] = Key;
    ++Depth;
    return Key;
}

// Inline in FUN_0056daa0 (0x0056dac3..0x0056db88) and FUN_00567350.
HKEY FRegKeyStack::Create(char const* SubKey, REGSAM Access)
{
    if (Depth <= 0 || Keys[Depth - 1] == nullptr || Depth >= 32)
        return nullptr;
    HKEY Key = nullptr;
    DWORD Disposition = 0;
    char Class[] = ""; // DAT_005c865c
    if (RegCreateKeyExA(Keys[Depth - 1], SubKey, 0, Class, 0, Access, nullptr, &Key,
                        &Disposition) != ERROR_SUCCESS)
        return nullptr;
    Keys[Depth] = Key;
    ++Depth;
    return Key;
}

// FUN_005b6630.
HKEY FRegKeyStack::Top() const
{
    return Depth > 0 ? Keys[Depth - 1] : nullptr;
}

// FUN_005b6b90.
LONG FRegKeyStack::SetString(char const* Name, char const* Value)
{
    return RegSetValueExA(Top(), Name, 0, REG_SZ, reinterpret_cast<BYTE const*>(Value),
                          static_cast<DWORD>(strlen(Value) + 1));
}
