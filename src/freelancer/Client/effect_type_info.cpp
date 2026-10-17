// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "effect_type_info.h"

#include "common_ini.h"
#include "common_misc.h" // ICamera
#include "dacom.h"
#include "perf_options.h" // GetPerfGenericEffectPerf (DAT_006129c8)

#include <string.h>

namespace
{
    char const* const kEffectLodIniNames[kEffectLodTypeCount] = {
        "none",
        "lod_type_trail",
        "lod_type_weapon",
        "lod_type_small",
        "lod_type_medium",
        "lod_type_large",
        "lod_type_small_distant",
    };

    // Names matched by [EffectType] lod_type= (table at .data:006131e8).
    char const* const kEffectLodTypeNames[kEffectLodTypeCount] = {
        "EFT_LOD_NONE",
        "EFT_LOD_TRAIL",
        "EFT_LOD_WEAPON",
        "EFT_LOD_SMALL",
        "EFT_LOD_MEDIUM",
        "EFT_LOD_LARGE",
        "EFT_LOD_SMALL_DISTANT",
    };

}

// PTR_s_EFT_NONE_00612a68 .. PTR_s_EFT_NUMTYPES_006131c0: an initialised .data table (0x2f rows
// of 0x28 bytes), no run-time seeding; [EffectType] sections overlay it in place. Every row
// below was diffed byte-for-byte against the retail PE's .data (2026-09-25, exe-w3b).
FEffectTypeInfo G_EffectTypeTable[kEffectTypeCount] = {
        {"EFT_NONE", 0.f, 0.f, 0, 10.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_SMALL", 1.f, 0.5f, 3, 10.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_MEDIUM", 1.f, 0.5f, 4, 50.f, 1, 1, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_LARGE", 1.f, 0.5f, 5, 100.f, 1, 1, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_DEBRIS", 0.f, 0.f, 3, 10.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_SHIP", 1.f, 0.5f, 4, 50.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_MISSILE", 0.f, 0.f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_EXPLOSION_MINE", 0.f, 0.f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_MUZZLE_FLASH", 0.f, 0.f, 6, 5.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_MUZZLE_LARGE_FLASH", 1.f, 0.5f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_MISSILE_DRIVE", 0.5f, 0.5f, 2, 100.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_WEAPON_PROJ", 1.f, 0.5f, 2, 15.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_WEAPON_LARGE_PROJ", 1.f, 0.5f, 5, 50.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_WEAPON_IMPACT", 0.5f, 0.5f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_WEAPON_LARGE_IMPACT", 1.f, 0.5f, 4, 20.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_WEAPON_COUNTER_MEASURE", 1.f, 0.5f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_WEAPON_MINE", 1.f, 0.5f, 4, 20.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_DAMAGE_HULL", 0.5f, 0.5f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_DAMAGE_LARGE_SHIELD", 1.f, 0.5f, 5, 100.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_DAMAGE_SHIELD", 0.5f, 0.5f, 3, 5.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_DAMAGE_CRUISE", 0.5f, 0.5f, 6, 7.5f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_DAMAGE_SMOKE", 0.5f, 0.5f, 4, 20.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_DAMAGE_LARGE_SMOKE", 0.5f, 0.5f, 5, 20.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_DEBRIS_TRAIL", 0.5f, 0.5f, 3, 5.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_ENGINE_CONTRAIL", 0.f, 0.f, 3, 5.f, 1, 1, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_ENGINE_TRAIL", 1.f, 0.5f, 1, 100.f, 1, 1, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_ENGINE_LARGE_TRAIL", 1.f, 0.5f, 1, 100.f, 1, 1, {0, 0}, -1.f, 5.f, 500.f, 2000.f},
        {"EFT_ENGINE_FIRE", 0.5f, 0.5f, 6, 7.5f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_ENGINE_LARGE_FIRE", 0.5f, 0.5f, 6, 25.f, 1, 0, {0, 0}, -1.f, 5.f, 250.f, 1000.f},
        {"EFT_ENGINE_THRUSTER", 0.f, 0.f, 6, 7.5f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_EQUIP_CLOAK", 1.f, 0.5f, 3, 5.f, 0, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_EQUIP_LARGE_CLOAK", 1.f, 0.5f, 4, 100.f, 0, 0, {0, 0}, -1.f, 5.f, 500.f, 1500.f},
        {"EFT_EQUIP_ATTACHED", 1.f, 0.5f, 5, 100.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_EQUIP_ATTACHED_LARGE", 1.f, 0.5f, 5, 500.f, 1, 0, {0, 0}, -1.f, 5.f, 1000.f, 5000.f},
        {"EFT_EQUIP_ATTACHED_INSANELY_LARGE", 1.f, 0.5f, 5, 500.f, 1, 0, {0, 0}, -1.f, 5.f, 1000.f, 5000.f},
        {"EFT_MISC_DUST", 0.5f, 0.5f, 0, 500.f, 1, 1, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_MOTION_DUST", 1.f, 1.f, 0, 500.f, 1, 1, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_JUMP_EFFECTS", 1.f, 0.5f, 0, 0.f, 1, 1, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_JUMP_GATE_EFFECTS", 1.f, 0.5f, 0, 0.f, 1, 1, {0, 0}, -1.f, 5.f, 1000.f, 2500.f},
        {"EFT_LANE_ACTIVE", 0.5f, 0.5f, 0, 50.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_LANE_TUBE", 1.f, 0.5f, 0, 50.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_LANE_SHIP", 0.5f, 0.5f, 4, 50.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_LANE_SHIP_TRAVEL", 0.5f, 0.5f, 4, 50.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_CUSTOM_EFFECTS", 0.f, 0.f, 5, -1.f, 0, 0, {0, 0}, 15.f, 5.f, 100.f, 500.f},
        {"EFT_CODE_PROJ_EFFECT", 1.f, 0.5f, 2, 15.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_TRACTORBEAM", 1.f, 1.f, 0, -1.f, 1, 1, {0, 0}, -1.f, 5.f, 100.f, 500.f},
        {"EFT_FLASHLIGHT", 1.f, 1.f, 0, -1.f, 1, 0, {0, 0}, -1.f, 5.f, 100.f, 500.f},
    };

// LAB_0042dac0 (_initterm slot 0x0060f1a0): EAX from 0x00667d98, 8 rows, [EAX-8]/[EAX-4]/[EAX] =
// 0xbf800000 (-1.0f).
FEffectLodSizes G_EffectLodTable[kEffectLodTableRows] = {
    {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
    {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
};

int EffectTypeIndexFromName(char const* Nickname)
{
    // FUN_004fd500 (cdecl): an inlined strcmp of every row's name against Nickname, EDI walking
    // 0x00612a68 by 0x28 up to PTR_s_EFT_NUMTYPES_006131c0; the returned EBP is the row index,
    // 0x2f when nothing matched. No null test on either string.
    int Index = 0;
    for (Index = 0; Index < kEffectTypeCount; ++Index)
    {
        if (strcmp(G_EffectTypeTable[Index].Nickname, Nickname) == 0)
            break;
    }
    return Index;
}

FEffectTypeInfo* EffectTypeInfoByIndex(int Index)
{
    // FUN_004fd560 (cdecl): `LEA EAX,[Index*5]; LEA EAX,[EAX*8 + 0x00612a68]` -- no range test.
    return &G_EffectTypeTable[Index];
}

bool EffectTypeWithinPerf(int Index)
{
    // FUN_004fd570 (cdecl): the row's Priority (DAT_00612a6c + Index * 0x28, no range test)
    // against FUN_004f70b0 = DAT_006129c8; FCOMP perf vs priority, TEST AH,0x41 / JP -> 1 when
    // exactly one of C0/C3 is set: perf below or equal to the priority, never when unordered.
    float const Perf = GetPerfGenericEffectPerf();
    float const Priority = G_EffectTypeTable[Index].Priority;
    return Perf < Priority || Perf == Priority;
}

bool LoadEffectTypeSection(INI_Reader* Reader)
{
    // FUN_004fd5a0 (cdecl, Reader in ESI). Current lives in EBX with a copy at [ESP+0x10]; that
    // copy is written only when a nickname resolves, so an unknown nickname keeps the previous
    // row (0x004fd669 reloads EBX from it) instead of clearing it. No null test on Reader.
    static char const* const kSourceFile = "E:\\FL\\Scratch\\Source\\Client\\Space\\EffectTypes.cpp";

    FEffectTypeInfo* Current = nullptr;
    bool bHaveValue = Reader->read_value();
    while (bHaveValue)
    {
        if (Reader->is_value("nickname"))
        {
            // 0x004fd5e4..0x004fd62e: the same inlined lookup as FUN_004fd500.
            char const* Nick = Reader->get_value_string();
            int Index = EffectTypeIndexFromName(Nick);
            if (Index >= kEffectTypeCount)
            {
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s", kSourceFile, 0x4c,
                      "EffectTypes: unknown type");
                Reader->log_link("EffectTypes");
            }
            else
            {
                Current = &G_EffectTypeTable[Index];
            }
        }
        else if (Current != nullptr)
        {
            if (Reader->is_value("priority"))
                Current->Priority = Reader->get_value_float(0);
            else if (Reader->is_value("generic_priority"))
                Current->GenericPriority = Reader->get_value_float(0);
            else if (Reader->is_value("lod_type"))
            {
                // 0x004fd6e6: EDI from 1 to 6; get_value_string() is re-read every pass.
                int Lod = 1;
                for (Lod = 1; Lod < kEffectLodTypeCount; ++Lod)
                {
                    if (_stricmp(Reader->get_value_string(), kEffectLodTypeNames[Lod]) == 0)
                    {
                        Current->LodType = Lod;
                        break;
                    }
                }
            }
            else if (Reader->is_value("radius"))
            {
                // FCOM 0.0 (DAT_005c7474), TEST AH,5 / JP: only a strictly negative value is
                // reported and replaced with 1.0 (DAT_005c75dc); NaN is stored as read.
                float Radius = Reader->get_value_float(0);
                if (Radius < 0.0f)
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: effect type (%s) has negative radius",
                          kSourceFile, 0x7c, Current->Nickname);
                    Radius = 1.0f;
                }
                Current->Radius = Radius;
            }
            else if (Reader->is_value("visibility"))
            {
                char const* Value = Reader->get_value_string();
                Current->VisibilityOffscreen =
                    static_cast<unsigned char>(_stricmp(Value, "EXIST_OFFSCREEN") == 0 ? 1 : 0);
            }
            else if (Reader->is_value("update"))
            {
                char const* Value = Reader->get_value_string();
                Current->UpdateOffscreen =
                    static_cast<unsigned char>(_stricmp(Value, "UPDATE_OFFSCREEN") == 0 ? 1 : 0);
            }
            else if (Reader->is_value("run_time"))
                Current->RunTime = Reader->get_value_float(0);
            else if (Reader->is_value("fps"))
                Current->Fps = Reader->get_value_float(0);
            else if (Reader->is_value("pbubble"))
            {
                // 0x004fd83d: FST far, FCOMP near, TEST AH,5 / JP -- warns only when far is
                // strictly below near.
                Current->PbubbleNear = Reader->get_value_float(0);
                Current->PbubbleFar = Reader->get_value_float(1);
                if (Current->PbubbleFar < Current->PbubbleNear)
                {
                    FDUMP(0x100002, "%s(%d) : WARNING:General:%s", kSourceFile, 0xc1,
                          "pbubble ranges backwards");
                    Reader->log_link("EffectTypes");
                }
            }
            else
            {
                // 0x004fd880: any other key.
                FDUMP(0x100001, "%s(%d) : *** ERROR: EffectType (%s) has unknown parameter (%s)",
                      kSourceFile, 0xc7, Current->Nickname, Reader->get_file_name());
            }
        }
        bHaveValue = Reader->read_value();
    }
    return Current != nullptr;
}

bool LoadEffectLodSection(INI_Reader* Reader)
{
    // FUN_0042dae0. The table's -1.0 defaults are the static initializer's (see above).
    bool bHaveValue = Reader->read_value();
    if (!bHaveValue || !Reader->is_value("type"))
        return false;

    char const* TypeName = Reader->get_value_string();
    unsigned Lod = 1;
    for (Lod = 1; Lod < static_cast<unsigned>(kEffectLodTypeCount); ++Lod)
    {
        if (_stricmp(TypeName, kEffectLodIniNames[Lod]) == 0)
            break;
    }
    if (Lod >= static_cast<unsigned>(kEffectLodTypeCount))
    {
        // 0042db85..0042dbb9: FDUMP the bad name, then log_link("") (.rdata 0x005c865c is an
        // empty string) and return false.
        FDUMP(0x100001, "%s(%d) : *** ERROR: invalid LOD type (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\VisEffectArch.cpp", 0x69, TypeName);
        Reader->log_link("");
        return false;
    }

    FEffectLodSizes* Row = &G_EffectLodTable[Lod];
    bHaveValue = Reader->read_value();
    if (!bHaveValue)
        return true;
    do
    {
        if (Reader->is_value("max_lod_screen_size"))
            Row->MaxLodScreenSize = Reader->get_value_float(0);
        else if (Reader->is_value("min_lod_screen_size"))
            Row->MinLodScreenSize = Reader->get_value_float(0);
        else if (Reader->is_value("min_screen_size"))
            Row->MinScreenSize = Reader->get_value_float(0);
        bHaveValue = Reader->read_value();
    } while (bHaveValue);
    return true;
}

bool EffectLodRowBlend(FEffectLodSizes const* Row, ICamera* Cam, float Distance, float Radius,
                       float* OutBlend)
{
    // FUN_0042da00. 0x0042da00: FCOMP the distance against the double 1e-5, TEST AH,5 / JP --
    // only strictly below gives 1.0 straight away.
    if (static_cast<double>(Distance) < 1e-05)
    {
        *OutBlend = 1.0f;
        return true;
    }

    // 0x0042da2b..0x0042da46: slot +0x2c first (stored as a float), then slot +0x20 times it;
    // Radius / Distance times that product, all on the FPU.
    float const XScale = static_cast<float>(Cam->GetXScaleOverNear());
    double const ZNear = Cam->GetZNear();
    double const ScreenSize =
        (static_cast<double>(Radius) / Distance) * (ZNear * static_cast<double>(XScale));

    // 0x0042da48: FCOM min_screen_size (+8), TEST AH,0x41 / JNZ -- at or below (or NaN) the
    // effect is not drawn: *Out = 0, false.
    if (!(ScreenSize > Row->MinScreenSize))
    {
        *OutBlend = 0.0f;
        return false;
    }

    // 0x0042da52..0x0042da6e: (size - min_lod) / (max_lod - min_lod); above 1.0
    // (TEST AH,0x41 / JNZ) -> 1.0; LAB_0042da98: strictly below 0.0 (TEST AH,5 / JP) -> 0.0.
    double Blend = (ScreenSize - Row->MinLodScreenSize) /
                   (static_cast<double>(Row->MaxLodScreenSize) - Row->MinLodScreenSize);
    if (Blend > 1.0)
    {
        *OutBlend = 1.0f;
        return true;
    }
    if (Blend < 0.0)
        Blend = 0.0;
    *OutBlend = static_cast<float>(Blend);
    return true;
}

bool EffectTypeLodBlend(int TypeIndex, ICamera* Cam, float Distance, float* OutBlend)
{
    // FUN_0042dcd0. FUN_004fd560 with retail's null test; lod_type must be 1..6 (JLE / JGE,
    // signed).
    FEffectTypeInfo const* const Info = EffectTypeInfoByIndex(TypeIndex);
    if (Info != nullptr && Info->LodType > 0 && Info->LodType < kEffectLodTypeCount)
        return EffectLodRowBlend(&G_EffectLodTable[Info->LodType], Cam, Distance, Info->Radius,
                                 OutBlend);
    // LAB_0042dd10.
    *OutBlend = 1.0f;
    return true;
}
