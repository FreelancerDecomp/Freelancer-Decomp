// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_cobjdb.h"

#include "common_archetype.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_reputation.h"
#include "common_text.h"
#include "common_universe.h"

#include <cstring>

// ============================================================================
// SolarReps — retail Common.dll FUN_062b8de0 / FUN_062b8c20 / FUN_062b8ab0
// (0x58a80 FindSolarRep). Called from Universe system-file load after open,
// before Zone/Object parse (LoadUniverseIni opens each System file= and runs
// this pass, then reset). Seeds Reputation::Vibe entries for every [Object]
// nickname so CSolar::ReadObj's Vibe::Verify gate can pass.
// ============================================================================

namespace
{
    // FUN_062b8ab0 (.text:062b8ab0-062b8c16). cdecl, five arguments; returns the vibe handle,
    // which is the nickname id copied out of *Nick (062b8ac8-062b8ad5) -- Vibe::Alloc does not
    // write it back.
    int AllocObjectVibe(ID_String const* Nick, unsigned IdsName, TString<16> const* Group,
                        unsigned Infocard, Archetype::Solar const* Arch)
    {
        int Handle = (int)Nick->Id;                                  // 062b8ac8-062b8ad5
        Reputation::Vibe::Alloc(Handle);                             // 062b8ad9

        // 062b8ae5-062b8af7: get_id(Group); SetAffiliation(&Handle, id, false). The false is
        // the EBX pushed before get_id's argument and left on the stack after ADD ESP,4.
        Reputation::Vibe::SetAffiliation(Handle, Reputation::get_id(*Group), false);

        // 062b8aff-062b8b7f: two FmtStr locals built inline (Head, MadLibId, 14 index bytes
        // zeroed). 062b8b91 begin_mad_lib(IdsName) (folded setter 06309b30) and 062b8b9a
        // end_mad_lib (empty 062a8920), both on the first one.
        FmtStr Name1;
        FmtStr Name2;
        Name1.begin_mad_lib(IdsName);
        Name1.end_mad_lib();
        Reputation::Vibe::SetName(Handle, Name1, Name2, nullptr);    // 062b8b9f-062b8baf

        Reputation::Vibe::SetInfocard(Handle, Infocard);             // 062b8bb4-062b8bbe

        // 062b8bc3-062b8bda: SetRank(&Handle, Arch->Toughness) only when an archetype resolved.
        if (Arch != nullptr)
            Reputation::Vibe::SetRank(Handle, (unsigned)Arch->Toughness);

        return Handle;                                               // 062b8be2 / 062b8c08
    }

    // FUN_062b8c20 (.text:062b8c20-062b8dd7). One [Object] section -> FUN_062b8ab0.
    int LoadOneObjectSection(INI_Reader& Reader)
    {
        // 062b8c27-062b8c48: Nick.clear(), Group {Length 0, Data[0] 0}, IdsName / Infocard /
        // Arch 0.
        ID_String Nick;
        Nick.clear();
        TString<16> Group;
        Group.Length = 0;
        Group.Data[0] = '\0';
        unsigned IdsName = 0;
        unsigned Infocard = 0;
        Archetype::Solar const* Arch = nullptr;

        if (Reader.read_value())                                     // 062b8c4c-062b8c53
        {
            do
            {
                // 062b8c60-062b8c77: the section stops being read once a name, a nickname and
                // a group are all known (IdsName != 0, !Nick.IsEmpty(), Group.Length != 0).
                if (IdsName != 0 && !Nick.IsEmpty() && Group.Length != 0)
                    break;

                if (Reader.is_value("nickname"))                     // 062b8c7d-062b8c9d
                {
                    Nick.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("ids_name"))                // 062b8ca6-062b8cbc
                {
                    IdsName = (unsigned)Reader.get_value_int(0);
                }
                else if (Reader.is_value("infocard_ids") || Reader.is_value("ids_info"))
                {
                    Infocard = (unsigned)Reader.get_value_int(0);    // 062b8d80-062b8d88
                }
                else if (Reader.is_value("group"))                   // 062b8ceb-062b8d46
                {
                    char const* Text = Reader.get_value_string();
                    if (Text == nullptr)
                    {
                        Group.Length = 0;
                        Group.Data[0] = '\0';
                    }
                    else
                    {
                        // 062b8d12-062b8d42: strlen, capped at 15, copied, terminated.
                        unsigned Len = (unsigned)std::strlen(Text);
                        if (Len >= 16u)
                            Len = 15u;
                        std::memcpy(Group.Data, Text, Len);
                        Group.Data[Len] = '\0';
                        Group.Length = Len;
                    }
                }
                else if (Reader.is_value("archetype"))               // 062b8d48-062b8d7a
                {
                    ID_String ArchName;
                    ArchName.Id = CreateID(Reader.get_value_string());
                    Arch = Archetype::GetSolar(Archetype::GetAsteroidID(ArchName));
                }
            } while (Reader.read_value());                           // 062b8d8c-062b8d95
        }

        // 062b8d9b-062b8dd7: no nickname -> -1, otherwise FUN_062b8ab0.
        if (Nick.IsEmpty())
            return -1;
        return AllocObjectVibe(&Nick, IdsName, &Group, Infocard, Arch);
    }
}

int __cdecl SolarReps::FindSolarRep(ID_String const& Id)
{
    // .text:062b8a80-062b8aa1: Verify(Id.Id) ? Id.Id : -1.
    if (Reputation::Vibe::Verify((int)Id.Id))
        return (int)Id.Id;
    return -1;
}

void __cdecl Reputation::SolarReps::LoadObjectVibesFromSystemIni(INI_Reader& Reader)
{
    // FUN_062b8de0 (.text:062b8de0-062b8e15): every "Object" header -> FUN_062b8c20; its
    // return value is dropped (062b8e06).
    while (Reader.read_header())
    {
        if (Reader.is_header("Object"))
            LoadOneObjectSection(Reader);
    }
}
