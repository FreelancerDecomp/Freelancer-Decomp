// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail FUN_00484090 — dealer equipment stat / tip text.
// Listing: freelancer.exe.txt 0x00484090–0x004854d8. Table: docs/re/dealer_equip_stats.md

#include "dealer_equip_stats.h"

#include "ids_rdl.h"
#include "ids_strings.h"
#include "nn_text_scratch.h"

#include "common_archetype.h"
#include "common_goods.h"

#include <cstdio>
#include <cstring>

namespace
{
    // The line and unit-suffix scratch are retail's shared DAT_0066fc60 / DAT_0066dc60
    // (nn_text_scratch.h). FUN_00484090 itself only ever writes 0xff chars into them.
    wchar_t* const G_DealerStatLine = G_NnTextScratchB; // DAT_0066fc60
    wchar_t* const G_DealerStatUnit = G_NnTextScratchA; // DAT_0066dc60

    // .rdata:DAT_005d1d00 / DAT_005d1cfc — thruster speed estimate.
    const float kThrusterSpeedBias = 48000.0f;
    const float kThrusterSpeedScale = 0.0016666667f; // 1/600

    int FtolTrunc(float Value)
    {
        // MSVCRT _ftol: convert toward zero.
        return static_cast<int>(Value);
    }

    void ClearLine()
    {
        G_DealerStatLine[0] = L'\0';
    }

    void FlushLine(RenderDisplayList& OutRdl)
    {
        const size_t Len = wcslen(G_DealerStatLine);
        AppendTextMaybeXmlRdl(G_DealerStatLine, static_cast<unsigned int>(Len), OutRdl, false);
        ClearLine();
    }

    void EmitLabel(unsigned Ids, bool bEmitLabels)
    {
        if (!bEmitLabels)
            return;
        GetIdsStringWWithRawFallback(Ids, G_DealerStatLine, 0xff);
    }

    void EmitNewlineOnly(wchar_t* Dest)
    {
        wcscpy(Dest, L"\n");
    }

    void EmitIntNewline(wchar_t* Dest, int Value)
    {
        _snwprintf(Dest, 255, L"%d\n", Value);
        Dest[255] = L'\0';
    }

    void EmitFloat2Newline(wchar_t* Dest, float Value)
    {
        _snwprintf(Dest, 255, L"%0.2f\n", static_cast<double>(Value));
        Dest[255] = L'\0';
    }

    void EmitIntNoNewline(wchar_t* Dest, int Value)
    {
        _snwprintf(Dest, 255, L"%d", Value);
        Dest[255] = L'\0';
    }

    void EmitPercentNewline(wchar_t* Dest, int Value)
    {
        _snwprintf(Dest, 255, L"%d%%\n", Value);
        Dest[255] = L'\0';
    }

    void AppendUnitSuffix(wchar_t* Dest, unsigned UnitIds)
    {
        GetIdsStringWWithRawFallback(UnitIds, G_DealerStatUnit, 0xff);
        wcscat(Dest, G_DealerStatUnit);
        wcscat(Dest, L"\n");
    }

    // HpAttachmentType band → class IDS; returns 0 if outside the five special bands.
    unsigned ClassIdsForHpType(HpAttachmentType Hp)
    {
        const int Value = static_cast<int>(Hp);
        if (Value >= 0x0f && Value <= 0x18)
            return 0x395;
        if (Value >= 0x19 && Value <= 0x22)
            return 0x396;
        if (Value >= 0x25 && Value <= 0x2e)
            return 0x397;
        if (Value >= 0x2f && Value <= 0x38)
            return 0x398;
        if (Value >= 0x39 && Value <= 0x42)
            return 0x399;
        return 0;
    }

    // Tech level 1..10 inside a special band; 0 if unknown (matches jumptable case 10).
    int TechLevelForHpType(HpAttachmentType Hp)
    {
        const int Value = static_cast<int>(Hp);
        int BandStart = 0;
        if (Value >= 0x0f && Value <= 0x18)
            BandStart = 0x0f;
        else if (Value >= 0x19 && Value <= 0x22)
            BandStart = 0x19;
        else if (Value >= 0x25 && Value <= 0x2e)
            BandStart = 0x25;
        else if (Value >= 0x2f && Value <= 0x38)
            BandStart = 0x2f;
        else if (Value >= 0x39 && Value <= 0x42)
            BandStart = 0x39;
        else
            return 0;
        return (Value - BandStart) + 1;
    }

    void EmitClassAndLevel(RenderDisplayList& OutRdl, HpAttachmentType Hp, bool bEmitLabels,
                           bool bEmitValues)
    {
        const unsigned ClassIds = ClassIdsForHpType(Hp);
        if (ClassIds == 0)
            return;

        EmitLabel(ClassIds, bEmitLabels);

        wchar_t ValueBuf[256];
        if (!bEmitValues)
            EmitNewlineOnly(ValueBuf);
        else
            EmitIntNewline(ValueBuf, TechLevelForHpType(Hp));

        wcscat(G_DealerStatLine, ValueBuf);
        FlushLine(OutRdl);
    }

    void EmitIntStat(RenderDisplayList& OutRdl, unsigned LabelIds, float Value, bool bEmitLabels,
                     bool bEmitValues)
    {
        EmitLabel(LabelIds, bEmitLabels);
        wchar_t ValueBuf[256];
        if (!bEmitValues)
            EmitNewlineOnly(ValueBuf);
        else
            EmitIntNewline(ValueBuf, FtolTrunc(Value));
        wcscat(G_DealerStatLine, ValueBuf);
        FlushLine(OutRdl);
    }

    void EmitIntStatWithUnit(RenderDisplayList& OutRdl, unsigned LabelIds, float Value,
                             unsigned UnitIds, bool bEmitLabels, bool bEmitValues)
    {
        EmitLabel(LabelIds, bEmitLabels);
        wchar_t ValueBuf[256];
        if (!bEmitValues)
        {
            EmitNewlineOnly(ValueBuf);
        }
        else
        {
            EmitIntNoNewline(ValueBuf, FtolTrunc(Value));
            AppendUnitSuffix(ValueBuf, UnitIds);
        }
        wcscat(G_DealerStatLine, ValueBuf);
        FlushLine(OutRdl);
    }

    void EmitFloatStat(RenderDisplayList& OutRdl, unsigned LabelIds, float Value, bool bEmitLabels,
                       bool bEmitValues)
    {
        EmitLabel(LabelIds, bEmitLabels);
        wchar_t ValueBuf[256];
        if (!bEmitValues)
            EmitNewlineOnly(ValueBuf);
        else
            EmitFloat2Newline(ValueBuf, Value);
        wcscat(G_DealerStatLine, ValueBuf);
        FlushLine(OutRdl);
    }

    void EmitPercentStat(RenderDisplayList& OutRdl, unsigned LabelIds, int Value, bool bEmitLabels,
                         bool bEmitValues)
    {
        EmitLabel(LabelIds, bEmitLabels);
        wchar_t ValueBuf[256];
        if (!bEmitValues)
            EmitNewlineOnly(ValueBuf);
        else
            EmitPercentNewline(ValueBuf, Value);
        wcscat(G_DealerStatLine, ValueBuf);
        FlushLine(OutRdl);
    }

    void LoadExplosionDamage(const ID_String& ExplosionName, float& OutHull, float& OutEnergy)
    {
        OutHull = 0.0f;
        OutEnergy = 0.0f;
        if (ExplosionName.IsEmpty())
            return;
        Archetype::Explosion* Explosion = Archetype::GetExplosion(ExplosionName);
        if (Explosion == nullptr)
            return;
        OutHull = Explosion->HullDamage;
        OutEnergy = Explosion->EnergyDamage;
    }

    float EnergyFromHullAndEnergy(float Hull, float Energy)
    {
        return Hull * ShieldEquipConsts::HULL_DAMAGE_FACTOR + Energy;
    }

    void BuildShieldGenerator(RenderDisplayList& OutRdl, Archetype::ShieldGenerator* Gen,
                              bool bEmitLabels, bool bEmitValues)
    {
        const HpAttachmentType Hp = Gen->get_hp_type();
        EmitClassAndLevel(OutRdl, Hp, bEmitLabels, bEmitValues);

        EmitIntStat(OutRdl, 0x6d8, Gen->MaxCapacity, bEmitLabels, bEmitValues);
        EmitIntStat(OutRdl, 0x6d9, Gen->RegenerationRate, bEmitLabels, bEmitValues);
        EmitIntStat(OutRdl, 0x6da, Gen->RebuildPowerDraw, bEmitLabels, bEmitValues);
    }

    void BuildThruster(RenderDisplayList& OutRdl, Archetype::Thruster* Thruster, bool bEmitLabels,
                       bool bEmitValues)
    {
        EmitIntStat(OutRdl, 0x3c6, Thruster->PowerUsage, bEmitLabels, bEmitValues);

        EmitLabel(0x6dc, bEmitLabels);
        wchar_t ValueBuf[256];
        if (!bEmitValues)
        {
            EmitNewlineOnly(ValueBuf);
        }
        else
        {
            const float Speed =
                (Thruster->MaxThrust + kThrusterSpeedBias) * kThrusterSpeedScale;
            EmitFloat2Newline(ValueBuf, Speed);
        }
        wcscat(G_DealerStatLine, ValueBuf);
        FlushLine(OutRdl);
    }

    HpAttachmentType MaxGunHpType(Archetype::Gun* Gun)
    {
        HpAttachmentType Best = static_cast<HpAttachmentType>(0);
        const int Count = Gun->get_number_of_hp_types();
        for (int Index = 0; Index < Count; ++Index)
        {
            const HpAttachmentType Hp = Gun->get_hp_type_by_index(Index);
            if (static_cast<int>(Best) < static_cast<int>(Hp))
                Best = Hp;
        }
        return Best;
    }

    void BuildGun(RenderDisplayList& OutRdl, Archetype::Gun* Gun, bool bEmitLabels,
                  bool bEmitValues)
    {
        EmitClassAndLevel(OutRdl, MaxGunHpType(Gun), bEmitLabels, bEmitValues);

        if (Gun->ProjectileArchetype.IsEmpty())
            return;
        Archetype::Equipment* ProjEquip = Archetype::GetEquipmentByName(Gun->ProjectileArchetype);
        if (ProjEquip == nullptr)
            return;
        if (ProjEquip->get_class_type() != Archetype::AClassMunition)
            return;

        Archetype::Munition* Mun = static_cast<Archetype::Munition*>(ProjEquip);

        float ExplHull = 0.0f;
        float ExplEnergy = 0.0f;
        LoadExplosionDamage(Mun->ExplosionArch, ExplHull, ExplEnergy);

        // Listing adds munition hull into the explosion-hull scratch before energy uses it.
        const float HullTotal = ExplHull + Mun->HullDamage;
        const float EnergyTotal =
            EnergyFromHullAndEnergy(HullTotal, ExplEnergy + Mun->EnergyDamage);

        EmitIntStat(OutRdl, 0x6cf, HullTotal, bEmitLabels, bEmitValues);
        EmitIntStat(OutRdl, 0x6d0, EnergyTotal, bEmitLabels, bEmitValues);

        // Range lines. MuzzleVelocity kept separately; guided path leaves SeekerRange on the
        // FPU for the first displayed value (listing 0x004846f3–0x00484753).
        const float MuzzleVelocity = Gun->MuzzleVelocity;
        const float SimpleRange = Mun->Lifetime * MuzzleVelocity;

        float FirstRange = SimpleRange;
        float SecondRange = MuzzleVelocity;

        if (bEmitValues)
        {
            if (!Mun->Motor.IsEmpty())
            {
                Archetype::MotorData const* Motor = Archetype::GetMotor(Mun->Motor.Id);
                if (Motor != nullptr && Motor->Lifetime > 0.0f)
                {
                    const float Guided = Mun->SeekerRange
                                         + (Motor->Lifetime - Motor->Delay) * Motor->Accel
                                         + MuzzleVelocity;
                    FirstRange = Mun->SeekerRange;
                    SecondRange = Guided;
                }
            }
        }

        EmitIntStatWithUnit(OutRdl, 0x6d1, FirstRange, 0x6df, bEmitLabels, bEmitValues);
        EmitIntStatWithUnit(OutRdl, 0x6d2, SecondRange, 0x6e0, bEmitLabels, bEmitValues);

        float Refire = 0.0f;
        if (Gun->RefireDelay > 0.0f)
            Refire = 1.0f / Gun->RefireDelay;
        EmitFloatStat(OutRdl, 0x6d3, Refire, bEmitLabels, bEmitValues);

        if (Mun->Seeker == 0)
        {
            EmitIntStat(OutRdl, 0x6d4, Gun->PowerUsage, bEmitLabels, bEmitValues);
        }
        else
        {
            EmitFloatStat(OutRdl, 0x3d1, Mun->MaxAngularVelocity, bEmitLabels, bEmitValues);
        }
    }

    void BuildMineDropper(RenderDisplayList& OutRdl, Archetype::MineDropper* Dropper,
                          bool bEmitLabels, bool bEmitValues)
    {
        if (Dropper->ProjectileArchetype.IsEmpty())
            return;
        Archetype::Equipment* ProjEquip = Archetype::GetEquipmentByName(Dropper->ProjectileArchetype);
        if (ProjEquip == nullptr)
            return;
        if (ProjEquip->get_class_type() != Archetype::AClassMine)
            return;

        Archetype::Mine* Mine = static_cast<Archetype::Mine*>(ProjEquip);

        float ExplHull = 0.0f;
        float ExplEnergy = 0.0f;
        LoadExplosionDamage(Mine->ExplosionArch, ExplHull, ExplEnergy);

        EmitIntStat(OutRdl, 0x6cf, ExplHull, bEmitLabels, bEmitValues);
        EmitIntStat(OutRdl, 0x6d0, EnergyFromHullAndEnergy(ExplHull, ExplEnergy), bEmitLabels,
                    bEmitValues);
        EmitIntStatWithUnit(OutRdl, 0x3b1, Mine->TopSpeed, 0x6e0, bEmitLabels, bEmitValues);
    }

    void BuildCounterMeasureStats(RenderDisplayList& OutRdl, Archetype::CounterMeasure* Cm,
                                  bool bEmitLabels, bool bEmitValues)
    {
        EmitIntStatWithUnit(OutRdl, 0x6d6, Cm->Range, 0x6df, bEmitLabels, bEmitValues);
        EmitPercentStat(OutRdl, 0x6d7, Cm->DiversionPctg, bEmitLabels, bEmitValues);
    }

    void BuildCmDropper(RenderDisplayList& OutRdl, Archetype::CounterMeasureDropper* Dropper,
                        bool bEmitLabels, bool bEmitValues)
    {
        Archetype::Equipment* ProjEquip = Archetype::GetEquipmentByName(Dropper->ProjectileArchetype);
        if (ProjEquip == nullptr)
            return;
        if (ProjEquip->get_class_type() != Archetype::AClassCounterMeasure)
            return;
        BuildCounterMeasureStats(OutRdl, static_cast<Archetype::CounterMeasure*>(ProjEquip),
                                 bEmitLabels, bEmitValues);
    }

    void BuildHitPtsConsumable(RenderDisplayList& OutRdl, Archetype::Equipment* Equip,
                               unsigned LabelIds, bool bEmitLabels, bool bEmitValues)
    {
        EmitIntStat(OutRdl, LabelIds, Equip->HitPts, bEmitLabels, bEmitValues);
    }

    void BuildExplosionProjectile(RenderDisplayList& OutRdl, Archetype::Equipment* Equip,
                                  bool bEmitLabels, bool bEmitValues)
    {
        if (Equip->ExplosionArch.IsEmpty())
            return;
        Archetype::Explosion* Explosion = Archetype::GetExplosion(Equip->ExplosionArch);
        if (Explosion == nullptr)
            return;

        EmitIntStat(OutRdl, 0x6cf, Explosion->HullDamage, bEmitLabels, bEmitValues);
        EmitIntStat(OutRdl, 0x6d0,
                    EnergyFromHullAndEnergy(Explosion->HullDamage, Explosion->EnergyDamage),
                    bEmitLabels, bEmitValues);
        EmitIntStatWithUnit(OutRdl, 0x6d5, Explosion->Radius, 0x6df, bEmitLabels, bEmitValues);
    }

    void BuildDefaultNewline(RenderDisplayList& OutRdl)
    {
        wcscpy(G_DealerStatLine, L"\n");
        FlushLine(OutRdl);
    }
}

void BuildDealerEquipmentStatText(RenderDisplayList& OutRdl, unsigned ArchId, bool bEmitLabels,
                                  bool bEmitValues)
{
    Archetype::Equipment* Equip = Archetype::GetEquipment(ArchId);
    if (Equip == nullptr)
        return;

    ClearLine();

    const Archetype::AClassType ClassType = Equip->get_class_type();
    switch (ClassType)
    {
    case Archetype::AClassShieldGenerator:
        BuildShieldGenerator(OutRdl, static_cast<Archetype::ShieldGenerator*>(Equip), bEmitLabels,
                             bEmitValues);
        break;
    case Archetype::AClassThruster:
        BuildThruster(OutRdl, static_cast<Archetype::Thruster*>(Equip), bEmitLabels, bEmitValues);
        break;
    case Archetype::AClassGun:
        BuildGun(OutRdl, static_cast<Archetype::Gun*>(Equip), bEmitLabels, bEmitValues);
        break;
    case Archetype::AClassMineDropper:
        BuildMineDropper(OutRdl, static_cast<Archetype::MineDropper*>(Equip), bEmitLabels,
                         bEmitValues);
        break;
    case Archetype::AClassCounterMeasureDropper:
        BuildCmDropper(OutRdl, static_cast<Archetype::CounterMeasureDropper*>(Equip), bEmitLabels,
                       bEmitValues);
        break;
    case Archetype::AClassRepairKit:
        BuildHitPtsConsumable(OutRdl, Equip, 0x6dd, bEmitLabels, bEmitValues);
        break;
    case Archetype::AClassShieldBattery:
        BuildHitPtsConsumable(OutRdl, Equip, 0x6de, bEmitLabels, bEmitValues);
        break;
    case Archetype::AClassMunition:
    case Archetype::AClassMine:
        BuildExplosionProjectile(OutRdl, Equip, bEmitLabels, bEmitValues);
        break;
    case Archetype::AClassCounterMeasure:
        BuildCounterMeasureStats(OutRdl, static_cast<Archetype::CounterMeasure*>(Equip),
                                 bEmitLabels, bEmitValues);
        break;
    default:
        BuildDefaultNewline(OutRdl);
        break;
    }
}
