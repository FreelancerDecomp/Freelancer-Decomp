// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal helpers shared by the Archetype translation units: common_archetype.cpp
// (where they are defined), common_archetype_ships.cpp, common_archetype_equipment.cpp,
// common_archetype_weapons.cpp and common_archetype_misc.cpp. Retail keeps all of this in one
// TU (E:\FL\Scratch\Source\Common\Archetype.cpp); none of these helpers is exported. Split out
// of common_archetype.cpp's anonymous namespaces (CMN-SPLIT phase B, 2026-10-14).

#include "common_archetype.h"
#include "common_ini.h"     // INI_Reader
#include "DALib.h"          // FEngineName (via engine/engbase.h; DALib.h must come first, it
                            // declares the distinct IEngine2 before engbase.h is seen)

int CallDacomStricmp(const char* Left, const char* Right);  // common_dacom_stricmp.cpp

namespace Archetype
{
    namespace CmnArchetype
    {
        // FEngineArchetypeName was a field-for-field copy of engbase.h's FEngineName; the two
        // are the same 8-byte {const char*, CRC32} record IEngine takes at slots 11/12/17.
        typedef FEngineName FEngineArchetypeName;

        // Retail __FILE__ of Archetype.cpp, the first argument of every FDUMP here.
        const char ArchetypeSource[] = "E:\\FL\\Scratch\\Source\\Common\\Archetype.cpp";

        // GetEquipmentByName(CreateID(Name)).
        Equipment* LookupLootAppearance(char const* Name);

        // DACOM stricmp walk over the 63-entry hp-type table at .data:063eca40; first match
        // stores its value and returns true, a miss returns false with OutValue untouched.
        bool TryLookupHpType(char const* Name, unsigned* OutValue);

        // "loot_appearance" key: Out = LookupLootAppearance(value), true; any other key false.
        bool ReadLootAppearance(INI_Reader& Reader, Equipment*& Out);

        // The VC6 vector<unsigned> copy constructor / operator= of the ShieldGenerator (+0xa8)
        // and Gun (+0xbc) hp-type arrays, kept as malloc'd {Begin, End, Cap} blocks.
        void CopyConstructHpTypes(unsigned*& Begin, unsigned*& End, unsigned*& Cap,
                                  unsigned const* SrcBegin, unsigned const* SrcEnd);
        void AssignHpTypes(unsigned*& Begin, unsigned*& End, unsigned*& Cap,
                           unsigned const* SrcBegin, unsigned const* SrcEnd);

        // ICliObj slot 0 / slot 1 on a companion; null skips.
        void CliObjAddRef(ICliObj* Companion);
        void CliObjRelease(ICliObj* Companion);
    }

    using namespace CmnArchetype;
}
