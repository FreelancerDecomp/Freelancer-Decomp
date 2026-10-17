// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll CFLIDMaker / CreateID / ID_String — Common.dll.c:143988–144269, :180414–180449, FLIDMaker.cpp.
// Decorated exports match retail Common.dll (linked into freelancer.exe, not a Common.dll project).

struct FFlidCrcState;

class COMMON_API CFLIDMaker
{
public:
    static CFLIDMaker* __cdecl GetInstance();
    static void __cdecl Shutdown();

    int Initialize();
    unsigned int CreateStaticID(char const* Name);
    unsigned int AllocateObjectID();

    // Retail export ?m_pTheMaker@CFLIDMaker@@2PAV1@A (public static, data 0x063fcf68).
    static CFLIDMaker* m_pTheMaker;

protected:
    CFLIDMaker();
    ~CFLIDMaker();

    // Copy ctor / operator= stay implicit: retail exports the compiler-generated
    // ??4CFLIDMaker@@QAEAAV0@ABV0@@Z (ICF-folded two-dword copy at 0x06280ee0) and no copy ctor.

private:
    FFlidCrcState* CrcState;
    unsigned int NextDynamicId;
};

COMMON_API unsigned int __cdecl CreateID(char const* Name);

// 4-byte nickname-id holder. Not a string: get_string formats Id as "0x"+hex into a rotating scratch.
// Retail exports only these three members (no class-level export: no operator=).
struct ID_String
{
public:
    unsigned int Id;

    COMMON_API void clear();
    COMMON_API bool IsEmpty() const;
    COMMON_API char const* get_string() const;
};

static_assert(sizeof(ID_String) == 4, "ID_String is a single uint");

COMMON_API unsigned int __cdecl MakeLocationID(unsigned int BaseId, char const* Name);
COMMON_API unsigned short __cdecl MakeId(char const* Name);
