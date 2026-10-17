// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll StringCache (retail StringCache.cpp): the static string pool, its case-insensitive
// intern index and the static owner whose destructor drops that index, plus StringAlloc,
// StringShutdown and CacheString::clear. Split out of common_ini.cpp.
//
// Static-initialisation order: GStringCacheOwner must be constructed (its atexit destructor
// registered) before the CacheString globals of the goods TUs (EquipDesc::CARGO_BAY_HP_NAME),
// so this TU keeps common_ini.cpp's slot in Common.vcxproj, first among its siblings.

#include "common_ini.h"
#include "common_archetype.h" // CacheString (clear lives with the StringCache pool)

#include "dacom.h"

#include <string.h>

#include <map>
#include <utility>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp, the import retail's StringCache tree calls.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char StringCacheSource[] = "E:\\FL\\Scratch\\Source\\Common\\StringCache.cpp";

    // StringCache.cpp. One static pool of 0x180000 bytes (.data 0640a810..0658a80f; the overflow
    // message prints it as 0x600 KB). Byte 0 is the shared empty string: StringAlloc(null / "")
    // and CacheString::clear both hand it out. Interned strings grow up from the bottom cursor,
    // temporary ones down from the top cursor; nothing is ever freed or moved, so every pointer
    // stays valid until the process ends (StringShutdown only drops the index).
    const unsigned int StringCachePoolBytes = 0x180000;
    char GStringCachePool[StringCachePoolBytes];                // DAT_0640a810
    unsigned int GStringCacheBottom = 1;                         // DAT_063edb6c (.data = 1)
    unsigned int GStringCacheTop = StringCachePoolBytes;         // DAT_063edb70 (.data = 0x180000)
    unsigned int GStringCacheInternedCount = 1;                  // DAT_063edb74 (.data = 1)
    unsigned int GStringCacheTemporaryCount = 0;                 // DAT_0658a810

    // The intern index (DAT_0658a814): a unique map keyed by the pooled copy, ordered by DACOM
    // stricmp (06333ea0 find, 06333f30 insert, 063341a0 node insert, 06334160 erase, 06333af0
    // clear), so interning is case-insensitive. The mapped value is the pooled pointer itself.
    struct FStringCacheLess
    {
        bool operator()(char const* Left, char const* Right) const
        {
            return CallDacomStricmp(Left, Right) < 0;
        }
    };

    typedef std::map<char const*, char*, FStringCacheLess> FStringCacheIndex;
    FStringCacheIndex* GStringCacheIndex = nullptr;              // DAT_0658a814

    // FUN_06333bb0 (thiscall on the StringCache object at DAT_0640a800, which it never reads):
    // operator new of the index the first time an interned string is requested.
    void EnsureStringCacheIndex()
    {
        if (GStringCacheIndex == nullptr)
            GStringCacheIndex = new FStringCacheIndex();
    }

    // StringShutdown 06333c70 and the atexit destructor LAB_06333ac0 share this body: clear the
    // tree (06333af0), free the index object, null the pointer. The pool and its cursors stay.
    void DestroyStringCacheIndex()
    {
        if (GStringCacheIndex == nullptr)
            return;
        delete GStringCacheIndex;
        GStringCacheIndex = nullptr;
    }

    // The static StringCache object at DAT_0640a800. Its static initializer LAB_06333ab0 only
    // registers LAB_06333ac0 with atexit; this destructor is that registration.
    struct FStringCacheOwner
    {
        ~FStringCacheOwner()
        {
            DestroyStringCacheIndex();
        }
    };

    FStringCacheOwner GStringCacheOwner;
}

char* __cdecl StringAlloc(char const* Text, bool bTemporary)
{
    // Common.dll.txt:06333ca0. null or "" -> the pool's shared empty string (06333d07).
    if (Text == nullptr || *Text == '\0')
        return GStringCachePool;

    if (bTemporary)
    {
        // 06333cc0..06333d47: not interned. The copy goes directly below the top cursor; the
        // check is len+1 > top-bottom (06333cdf JBE = fits).
        unsigned int const Length = (unsigned int)strlen(Text);
        if (Length + 1u > GStringCacheTop - GStringCacheBottom)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: StringCache overflow! (%d KB)", StringCacheSource,
                  0xb9, 0x600);
            return GStringCachePool;
        }
        // 06333d14 SUB EBX,len; LEA EAX,[EBX + pool-1]; 06333d33 DEC EBX -> top -= len+1 and the
        // copy (through its NUL, 06333d22 loop) starts at pool + new top.
        GStringCacheTop -= Length + 1u;
        char* const Copy = GStringCachePool + GStringCacheTop;
        memcpy(Copy, Text, Length + 1u);
        GStringCacheTemporaryCount += 1u;
        return Copy;
    }

    // 06333d48: create the index on first use, then look the text up (06333ea0 = map::find with
    // the DACOM stricmp order). A hit with a non-null value returns the interned pointer.
    EnsureStringCacheIndex();
    FStringCacheIndex::iterator const Found = GStringCacheIndex->find(Text);
    if (Found != GStringCacheIndex->end() && Found->second != nullptr)
        return Found->second;

    // 06333d85..06333da3: same space check against the two cursors (line 0xa9 on this path).
    unsigned int const Length = (unsigned int)strlen(Text);
    if (Length + 1u > GStringCacheTop - GStringCacheBottom)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: StringCache overflow! (%d KB)", StringCacheSource,
              0xa9, 0x600);
        return GStringCachePool;
    }

    // 06333dd7..06333e25: copy at the bottom cursor, insert {copy, 0} (06333f30, unique insert:
    // an existing key keeps its node), store the copy as the node's value, advance the cursor.
    char* const Copy = GStringCachePool + GStringCacheBottom;
    memcpy(Copy, Text, Length + 1u);
    std::pair<FStringCacheIndex::iterator, bool> const Inserted =
        GStringCacheIndex->insert(FStringCacheIndex::value_type(Copy, nullptr));
    Inserted.first->second = Copy;
    GStringCacheInternedCount += 1u;
    GStringCacheBottom = GStringCacheBottom + Length + 1u;
    return Copy;
}

void CacheString::clear()
{
    // Common.dll.txt:06333e30 `MOV [this],DAT_0640a810`: the pool's shared empty string, the same
    // pointer StringAlloc(null / "") returns.
    Value = GStringCachePool;
}


void __cdecl StringShutdown()
{
    // Common.dll.txt:06333c70: if the index exists, clear it (06333af0), free it, null
    // DAT_0658a814. The pool cursors are not reset, so later StringAlloc calls keep appending.
    DestroyStringCacheIndex();
}
