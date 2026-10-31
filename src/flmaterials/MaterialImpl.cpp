// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_color_quantization.h"
#include "flmaterials.h"
#include "engine/engbase.h" // IEngine
#include "readfile.h"
#include "render/shading.h" // ITextureLibrary2, IMaterialLibrary, IMatAnimLibrary, IImaInstance
#include "x86math.h"        // I3DMathEngine

#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
    // FUN_066f14c0: legacy bounded formatter, result is the supplied buffer.
    char* FlmFormatMessage(char* Buffer, const char* Format, ...)
    {
        va_list Arguments;
        va_start(Arguments, Format);
        _vsnprintf(Buffer, 0x2000, Format, Arguments);
        va_end(Arguments);
        return Buffer;
    }

    const char* const kImplFile = "E:\\FL\\Scratch\\libs\\include\\IMaterialImpl.h";
    const char* const kWire2File = "E:\\FL\\Scratch\\Source\\FLMaterials\\WireMaterial2.cpp";

    // RS/TSS table: start state/type in [0].
    const unsigned long kGlassTss0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    const unsigned long kGlassTss1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};
    const unsigned long kGlassRs[] = {0x89, 1, 0x1D, 0, 0x1B, 0, 0};

    const unsigned long kHudIconRsOpaque[] = {0x89, 1, 0x1D, 0, 0x1B, 0, 0};
    const unsigned long kHudIconRsBlend[] = {0x89, 1, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    // HUDAnim DAT_067030fc has the same values as Glass stage 1.
    const unsigned long kHudAnimTss1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    const unsigned long kNomadRs[] = {0x89, 0, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    const unsigned long kNomadTss0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    const unsigned long kNomadTssTail[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    const unsigned long kWireRsBlend[] = {0x89, 0, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    const unsigned long kWireRsPlain[] = {0x89, 0, 0x1D, 0, 0x1B, 0, 0};
    const unsigned long kWireTss0[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    const unsigned long kWire2Rs[] = {0x89, 0, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    const unsigned long kWire2Tss0[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    const unsigned long kCloakTss[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};
    const unsigned long kCloakRsBlend[] = {0x89, 1, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    const unsigned long kCloakRsPlain[] = {0x89, 1, 0x1D, 0, 0x1B, 0, 0};

    const unsigned long kEngineGlowTss[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};
    const unsigned long kEngineGlowRs[] = {0x89, 1, 0x1D, 0, 0x1B, 0, 0};

    const unsigned long kExclusionTss0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    const unsigned long kExclusionTss1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};
    const unsigned long kExclusionRs[] = {0x89, 1, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};

    const unsigned long kPlanetTss0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    const unsigned long kPlanetTss1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};
    const unsigned long kPlanetRsBlend[] = {0x89, 1, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    const unsigned long kPlanetRsPlain[] = {0x89, 1, 0x1D, 0, 0x1B, 0, 0};

    unsigned char G_EnvMapGlassGuard = 0;
    unsigned long G_EnvMapGlassCrc = 0;
    void FlmDestroyEnvMapGlassCrc() {} // 066f6320: RET, registered at first CRC use.

    const unsigned long kEnvMapTss0[] = {1, 5, 2, 2, 3, 1, 4, 4, 5, 2, 6, 1, 0};
    const unsigned long kEnvMapTss1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    const unsigned long kShadowTss1[] = {1, 4, 2, 2, 3, 1, 4, 4, 5, 2, 6, 1, 0};

    // DAT_0670402C = 0x3F4CCCCD; DAT_067016D8 = double 1.0; DAT_06702818 ≈ 0.99.
    // Raw PE DAT_06704098 (22 D3D/FourCC IDs), DAT_067040f0 (15 records).
    const unsigned long GFlmPixelFormatIds[22] = {0x0ul, 0x29ul, 0x14ul, 0x17ul, 0x18ul, 0x1aul, 0x19ul, 0x15ul, 0x16ul, 0x0ul, 0x31545844ul, 0x32545844ul, 0x33545844ul, 0x34545844ul, 0x35545844ul, 0x504f4144ul, 0x544f4144ul, 0x41414144ul, 0x4c414144ul, 0x31414144ul, 0x34414144ul, 0x38414144ul};
    const unsigned long GFlmPixelFormatDescriptors[15][19] = {
        {0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0x1ul, 0x8ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0x2ul, 0x18ul, 0x8ul, 0xff0000ul, 0x10ul, 0x0ul, 0x8ul, 0xff00ul, 0x8ul, 0x0ul, 0x8ul, 0xfful, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0x3ul, 0x10ul, 0x5ul, 0xf800ul, 0xbul, 0x3ul, 0x6ul, 0x7e0ul, 0x5ul, 0x2ul, 0x5ul, 0x1ful, 0x0ul, 0x3ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0x4ul, 0x10ul, 0x5ul, 0x7c00ul, 0xaul, 0x3ul, 0x5ul, 0x3e0ul, 0x5ul, 0x3ul, 0x5ul, 0x1ful, 0x0ul, 0x3ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0x5ul, 0x10ul, 0x4ul, 0xf00ul, 0x8ul, 0x4ul, 0x4ul, 0xf0ul, 0x4ul, 0x4ul, 0x4ul, 0xful, 0x0ul, 0x4ul, 0x4ul, 0xf000ul, 0xcul, 0x4ul, 0x1ul},
        {0x6ul, 0x10ul, 0x5ul, 0x7c00ul, 0xaul, 0x3ul, 0x5ul, 0x3e0ul, 0x5ul, 0x3ul, 0x5ul, 0x1ful, 0x0ul, 0x3ul, 0x1ul, 0x8000ul, 0xful, 0x7ul, 0x1ul},
        {0x7ul, 0x20ul, 0x8ul, 0xff0000ul, 0x10ul, 0x0ul, 0x8ul, 0xff00ul, 0x8ul, 0x0ul, 0x8ul, 0xfful, 0x0ul, 0x0ul, 0x8ul, 0xff000000ul, 0x18ul, 0x0ul, 0x1ul},
        {0x8ul, 0x20ul, 0x8ul, 0xff0000ul, 0x10ul, 0x0ul, 0x8ul, 0xff00ul, 0x8ul, 0x0ul, 0x8ul, 0xfful, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0x9ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul},
        {0xaul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x1ul, 0x0ul, 0x0ul, 0x0ul, 0x1ul},
        {0xbul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x1ul},
        {0xcul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x1ul},
        {0xdul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x1ul},
        {0xeul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x0ul, 0x1ul},
    };

    // FUN_06700680: return the format-ID array, not an EH table.
    const unsigned long* FlmPixelFormatIds()
    {
        return GFlmPixelFormatIds;
    }

    // FUN_06700690: first matching ID among 22 words; absence returns index 0.
    int FlmFindPixelFormatIndex(unsigned long Format)
    {
        const unsigned long* Ids = FlmPixelFormatIds();
        for (int Index = 0; Index < 22; ++Index)
        {
            if (Ids[Index] == Format)
                return Index;
        }
        return 0;
    }

    // FUN_067006b0: signed Index>14 returns descriptor 0; else stride 0x4c.
    const unsigned long* FlmPixelFormatDescriptor(int Index)
    {
        return GFlmPixelFormatDescriptors[Index > 14 ? 0 : Index];
    }

    const float kGlassRaiseThreshold = 0.8f;
    const double kCloakColorizeThreshold = 1.0;
    const double kWireBlendThreshold = 0.99;

    // DAT_06702810 is double 255. MSVCRT ftol truncates via a temporary
    // x87 rounding mode and FISTP qword; keep its low byte, with no clamp.
    const double GFlmWireColorScale = 255.0;
    unsigned char FlmWireColorByte(float Component)
    {
        return static_cast<unsigned char>(PortableColorQuantization::ToInt64Bits(&Component));
    }


    // Size of one declared property descriptor.
    const int kFlmParamDescSize = 0x4C;

    // Size of one per-instance texture verification record.
    const unsigned long kFlmTextureVerifyEntrySize = 0x2C;

    // DAT_067013ec raw PE bits 0x34000000: 2^-23, kept unspilled in x87 bounds.
    const double kFlmVerifyEpsilon = 0x1p-23;

    // Colorize declared property descriptor; some fields are type-dependent.
    struct FFlmMaterialParamDesc
    {
        unsigned long Path; // see comment above -- EntryFlags for indices 8/9, else 0
        const char* ShortTag;
        const char* Name;
        const char* Field0C; // "" for entries 0-9 (ESI = 067016d4), "flcopt" for entry 10
        unsigned long Type0;
        unsigned long Value0[4];
        unsigned long Type1;
        unsigned long Value1[4];
        unsigned long Type2;
        unsigned long Value2[4];
    };

    static_assert(sizeof(FFlmMaterialParamDesc) == sizeof(FShadingPropertyRecord), "shared property descriptor size");
    static_assert(offsetof(FFlmMaterialParamDesc, Type0) == offsetof(FShadingPropertyRecord, Current), "current value boundary");
    static_assert(offsetof(FFlmMaterialParamDesc, Type1) == offsetof(FShadingPropertyRecord, Default), "default value boundary");
    static_assert(offsetof(FFlmMaterialParamDesc, Type2) == offsetof(FShadingPropertyRecord, Saved), "saved value boundary");

    void FlmSetDescriptorEntry(FFlmMaterialParamDesc* Entry, int Type,
                                unsigned long Flags, const char* Tag, const char* Name,
                                const char* Metadata, const void* Value0,
                                const void* Value1, const void* Value2);
    void FlmResetDescriptorEntry(unsigned long* Entry);

    // Native descriptor objects keep the three typed resources alive until
    // module teardown. An array gives reverse destruction and EH cleanup.
    struct FFlmOwnedDescriptor
    {
        FFlmMaterialParamDesc Entry = {};
        ~FFlmOwnedDescriptor() noexcept(false)
        {
            FlmResetDescriptorEntry((unsigned long*)(&Entry));
        }
    };

    template<unsigned long Count>
    struct FFlmOwnedParameterTable
    {
        FFlmOwnedDescriptor Descriptors[Count];
        // Slot27 constructs/registers destruction before filling defaults.
        // The published first tag independently controls every later rebuild.
        void InitializeDefaults(const FFlmMaterialParamDesc* Seeds)
        {
            if (Descriptors[0].Entry.Type0 != 0)
                return;
            for (unsigned long Index = 0; Index < Count; ++Index)
            {
                const FFlmMaterialParamDesc& Seed = Seeds[Index];
                const bool bVector = Seed.Type0 >= 2 && Seed.Type0 <= 4;
                const void* Value0 = bVector ? Seed.Value0 : (const void*)(Seed.Value0[0]);
                const void* Value1 = bVector ? Seed.Value1 : (const void*)(Seed.Value1[0]);
                const void* Value2 = bVector ? Seed.Value2 : (const void*)(Seed.Value2[0]);
                FlmSetDescriptorEntry(&Descriptors[Index].Entry, (int)(Seed.Type0),
                                      Seed.Path, Seed.ShortTag, Seed.Name, Seed.Field0C,
                                      Value0, Value1, Value2);
            }
        }
    };
    static_assert(sizeof(FFlmOwnedDescriptor) == kFlmParamDescSize, "Descriptor stride");


    void FlmReleaseTypedValue(unsigned long* Slot);
    void FlmSetDescriptorSubBlockValue(unsigned long* SubBlock, int NewType, const void* ValuePtr);

    struct FFlmWireParameterState
    {
        FFlmMaterialParamDesc Entry;
        bool bConstructed;
    };
    FFlmWireParameterState GFlmWireParameters = {};
    FFlmWireParameterState GFlmWire2Parameters = {};

    // LAB_066f3ca0: only the first payload word of each typed block is cleared.
    FFlmMaterialParamDesc* FlmConstructWireDescriptor(FFlmMaterialParamDesc* Entry)
    {
        Entry->Type0 = 0;
        Entry->Value0[0] = 0;
        Entry->Type1 = 0;
        Entry->Value1[0] = 0;
        Entry->Type2 = 0;
        Entry->Value2[0] = 0;
        Entry->Path = 0;
        Entry->ShortTag = nullptr;
        Entry->Name = nullptr;
        Entry->Field0C = nullptr;
        return Entry;
    }

    // FUN_066fba30 / FUN_066fc7f0: each callback owns its separate one-entry table.
    void FlmDestroyWireParameters()
    {
        FlmResetDescriptorEntry((unsigned long*)(&GFlmWireParameters.Entry));
    }

    void FlmDestroyWire2Parameters()
    {
        FlmResetDescriptorEntry((unsigned long*)(&GFlmWire2Parameters.Entry));
    }

    // LAB_066fb090 / LAB_066fbd20: construction guard precedes construction;
    // Type0 independently gates the mutable defaults on every invocation.
    void* FlmWireColorDescBase(bool bWire2)
    {
        FFlmWireParameterState& State = bWire2 ? GFlmWire2Parameters : GFlmWireParameters;
        if (!State.bConstructed)
        {
            State.bConstructed = true;
            FlmConstructWireDescriptor(&State.Entry);
            std::atexit(bWire2 ? FlmDestroyWire2Parameters : FlmDestroyWireParameters);
        }
        FFlmMaterialParamDesc& Entry = State.Entry;
        if (Entry.Type0 == 0)
        {
            // Raw PE: 067016bc=Dc, 06702808=Color, 067016d4=empty;
            // Wire 067026cc / Wire2 06702868 both contain four 1.0f words.
            const unsigned long White[4] = {0x3f800000ul, 0x3f800000ul, 0x3f800000ul, 0x3f800000ul};
            Entry.Path = 0;
            Entry.ShortTag = "Dc";
            Entry.Name = "Color";
            Entry.Field0C = "";
            FlmReleaseTypedValue(&Entry.Type0);
            Entry.Value0[3] = White[3];
            Entry.Value0[0] = White[0];
            const bool bReleaseSecond = Entry.Type1 != 4;
            Entry.Type0 = 4;
            Entry.Value0[2] = White[2];
            Entry.Value0[1] = White[1];
            if (bReleaseSecond)
                FlmReleaseTypedValue(&Entry.Type1);
            // Wire 067026bc / Wire2 06702858: four zero words.
            Entry.Value1[3] = 0;
            Entry.Value1[0] = 0;
            Entry.Type1 = 4;
            Entry.Value1[2] = 0;
            Entry.Value1[1] = 0;
            FlmSetDescriptorSubBlockValue(&Entry.Type2, 4, White);
        }
        return &Entry;
    }

    const void* FlmColorizeParamsBase(bool bCloak)
    {
        const unsigned long kF0 = 0x00000000ul; // 0.0f
        const unsigned long kF1 = 0x3f800000ul; // 1.0f
        static const FFlmMaterialParamDesc kParams[11] = {
            // 0: Ambient Color / Ac / type4
            {0, "Ac", "Ambient Color", "",
             4, {kF1, kF1, kF1, kF1}, 4, {kF0, kF0, kF0, kF0}, 4, {kF1, kF1, kF1, kF1}},
            // 1: Diffuse Color / Dc / type3
            {0, "Dc", "Diffuse Color", "",
             3, {kF1, kF1, kF1, 0}, 3, {kF0, kF0, kF0, 0}, 3, {kF1, kF1, kF1, 0}},
            // 2: Emissive Color / Ec / type3
            {0, "Ec", "Emissive Color", "",
             3, {kF0, kF0, kF0, 0}, 3, {kF0, kF0, kF0, 0}, 3, {kF1, kF1, kF1, 0}},
            // 3: Specular Color / Sc / type3
            {0, "Sc", "Specular Color", "",
             3, {kF0, kF0, kF0, 0}, 3, {kF0, kF0, kF0, 0}, 3, {kF1, kF1, kF1, 0}},
            // 4: Specular Power / Sp / type2
            {0, "Sp", "Specular Power", "",
             2, {kF0, 0, 0, 0}, 2, {kF0, 0, 0, 0}, 2, {kF1, 0, 0, 0}},
            // 5: Opacity / Oc / type2
            {0, "Oc", "Opacity ", "",
             2, {kF1, 0, 0, 0}, 2, {kF0, 0, 0, 0}, 2, {kF1, 0, 0, 0}},
            // 6: Texture Name / Dt_name / type7 (string) -- default "(None)"
            {0, "Dt_name", "Texture Name", "",
             7, {(unsigned long)("(None)"), 0, 0, 0},
             7, {0, 0, 0, 0}, 7, {0, 0, 0, 0}},
            // 7: Texture Wrap Mode / Dt_flags / type8 (nibble), default nibble=4
            {0, "Dt_flags", "Texture Wrap Mode", "",
             8, {4, 0, 0, 0}, 8, {0, 0, 0, 0}, 8, {0, 0, 0, 0}},
            // 8: Texture U Address Mode / Dt_flags / type9, Path=EntryFlags 0x20000
            {0x20000ul, "Dt_flags", "Texture U Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
            // 9: Texture V Address Mode / Dt_flags / type9, Path=EntryFlags 0x40000 -- built
            // via FUN_066f3de0 at 066f49b9: (9, 0x40000, "Dt_flags", Name, ESI="", 0, 0, 0).
            {0x40000ul, "Dt_flags", "Texture V Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
            // 10: Colorize Options / type1 -- built via FUN_066f3de0 at 066f49d4:
            // (1, 0, NULL, Name, "flcopt", 0, 0, 0). No +4 tag; "flcopt" sits at +0xC.
            {0, nullptr, "Colorize Options", "flcopt",
             1, {0, 0, 0, 0}, 1, {0, 0, 0, 0}, 1, {0, 0, 0, 0}},
        };
        static_assert(sizeof(FFlmMaterialParamDesc) == kFlmParamDescSize,
                      "FFlmMaterialParamDesc must match the confirmed 0x4C stride");
        if (bCloak)
        {
            static FFlmOwnedParameterTable<10> CloakTable;
            CloakTable.InitializeDefaults(kParams);
            return &CloakTable.Descriptors[0].Entry;
        }
        static FFlmOwnedParameterTable<11> Table;
        Table.InitializeDefaults(kParams);
        return &Table.Descriptors[0].Entry;
    }

    // Each material family owns its contiguous declared-property schema.
    const void* FlmGlassParamsBase(EMaterialKind Kind)
    {
        const unsigned long kZero = 0;
        const unsigned long kOne = 0x3f800000ul;
        static const FFlmMaterialParamDesc kParams[10] = {
            {0, "Ac", "Ambient Color", "",
             3, {kOne, kOne, kOne, 0}, 3, {kZero, kZero, kZero, 0},
             3, {kOne, kOne, kOne, 0}},
            {0, "Dc", "Diffuse Color", "",
             3, {kOne, kOne, kOne, 0}, 3, {kZero, kZero, kZero, 0},
             3, {kOne, kOne, kOne, 0}},
            {0, "Ec", "Emissive Color", "",
             3, {kZero, kZero, kZero, 0}, 3, {kZero, kZero, kZero, 0},
             3, {kOne, kOne, kOne, 0}},
            {0, "Sc", "Specular Color", "",
             3, {kZero, kZero, kZero, 0}, 3, {kZero, kZero, kZero, 0},
             3, {kOne, kOne, kOne, 0}},
            {0, "Sp", "Specular Power", "",
             2, {kZero, 0, 0, 0}, 2, {kZero, 0, 0, 0},
             2, {kOne, 0, 0, 0}},
            {0, "Oc", "Opacity ", "",
             2, {kOne, 0, 0, 0}, 2, {kZero, 0, 0, 0},
             2, {kOne, 0, 0, 0}},
            {0, "Dt_name", "Texture Name", "",
             7, {(unsigned long)("(None)"), 0, 0, 0},
             7, {0, 0, 0, 0}, 7, {0, 0, 0, 0}},
            {0, "Dt_flags", "Texture Wrap Mode", "",
             8, {4, 0, 0, 0}, 8, {0, 0, 0, 0}, 8, {0, 0, 0, 0}},
            {0x20000ul, "Dt_flags", "Texture U Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
            {0x40000ul, "Dt_flags", "Texture V Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
        };
        switch (Kind)
        {
        case KindEngineGlow:
        {
            static FFlmOwnedParameterTable<10> Table;
            Table.InitializeDefaults(kParams);
            return &Table.Descriptors[0].Entry;
        }
        case KindHudIcon:
        {
            static FFlmOwnedParameterTable<10> Table;
            Table.InitializeDefaults(kParams);
            return &Table.Descriptors[0].Entry;
        }
        case KindPlanetWater:
        {
            static FFlmOwnedParameterTable<10> Table;
            Table.InitializeDefaults(kParams);
            return &Table.Descriptors[0].Entry;
        }
        case KindHudAnim:
        {
            static FFlmOwnedParameterTable<10> Table;
            Table.InitializeDefaults(kParams);
            return &Table.Descriptors[0].Entry;
        }
        case KindGlass:
        {
            // Glass/HighGlass/GFGlass factories use 066f7190 / 06705300;
            // Exclusion uses the independent 066f66e0 / 06705600 table.
            static FFlmOwnedParameterTable<10> Table;
            Table.InitializeDefaults(kParams);
            return &Table.Descriptors[0].Entry;
        }
        default: // Exclusion inherits the base Glass Slot27.
        {
            static FFlmOwnedParameterTable<10> Table;
            Table.InitializeDefaults(kParams);
            return &Table.Descriptors[0].Entry;
        }
        }
    }

    const void* FlmNomadParamsBase()
    {
        const unsigned long kZero = 0;
        const unsigned long kOne = 0x3f800000ul;
        static const FFlmMaterialParamDesc kParams[9] = {
            {0, "Dc", "Diffuse Color", "",
             3, {kOne, kOne, kOne, 0}, 3, {kZero, kZero, kZero, 0},
             3, {kOne, kOne, kOne, 0}},
            {0, "Dt_name", "Diffuse Name", "",
             7, {(unsigned long)("(None)"), 0, 0, 0},
             7, {0, 0, 0, 0}, 7, {0, 0, 0, 0}},
            {0, "Dt_flags", "Diffuse Wrap Mode", "",
             8, {4, 0, 0, 0}, 8, {0, 0, 0, 0}, 8, {0, 0, 0, 0}},
            {0x20000ul, "Dt_flags", "Diffuse U Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
            {0x40000ul, "Dt_flags", "Diffuse V Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
            {0, "nt_name", "NomadTexture Name", "",
             7, {(unsigned long)("NomadRGB1_NomadAlpha1"), 0, 0, 0},
             7, {0, 0, 0, 0}, 7, {0, 0, 0, 0}},
            {0, "nt_flags", "NomadTexture Wrap Mode", "",
             8, {5, 0, 0, 0}, 8, {0, 0, 0, 0}, 8, {0, 0, 0, 0}},
            {0x20000ul, "nt_flags", "NomadTexture U Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
            {0x40000ul, "nt_flags", "NomadTexture V Address Mode", "",
             9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}, 9, {0, 0, 0, 0}},
        };
        static FFlmOwnedParameterTable<9> Table;
        Table.InitializeDefaults(kParams);
        return &Table.Descriptors[0].Entry;
    }

    // Typed property values own strings and interfaces according to their current type.

    // Release the value resource for its current type, then clear the payload.
    void FlmReleaseTypedValue(unsigned long* Slot)
    {
        const unsigned long OldType = Slot[0];
        if (OldType == 5 || OldType == 7)
        {
            if (Slot[1] != 0)
            {
                HeapFree(GetProcessHeap(), 0, (void*)(Slot[1]));
                Slot[1] = 0;
            }
            return;
        }
        if (OldType == 6)
        {
            IDAComponent* Old = (IDAComponent*)(Slot[1]);
            Slot[1] = 0;
            if (Old != nullptr)
                Old->Release();
            Slot[1] = 0; // Retail repeats the clear after the Release callback.
            return;
        }
        Slot[1] = 0;
    }

    // Typed strings use the process heap because their values cross material boundaries.
    void FlmAssignString(unsigned long* Slot, const char* NewStr)
    {
        if (Slot[1] != 0)
        {
            HeapFree(GetProcessHeap(), 0, (void*)(Slot[1]));
            Slot[1] = 0;
        }
        if (NewStr != nullptr)
        {
            const size_t Len = std::strlen(NewStr) + 1;
            void* Copy = HeapAlloc(GetProcessHeap(), 0, Len);
            Slot[1] = (unsigned long)(Copy);
            std::memcpy(Copy, NewStr, Len);
        }
    }

    // FUN_066f1b80 (flmaterials.dll.txt 5249-5273 / .c 896-910, clean decompile): owned-
    // interface-pointer swap -- AddRef the new pointer before Release-ing the old one.
    void FlmAssignInterface(unsigned long* Slot, IDAComponent* NewIface)
    {
        IDAComponent* Old = (IDAComponent*)(Slot[1]);
        Slot[1] = (unsigned long)(NewIface);
        if (NewIface != nullptr)
            NewIface->AddRef();
        if (Old != nullptr)
            Old->Release();
    }

    // Raw property storage needs its own assignment path, distinct from typed values.
    void FlmAssignStringRaw(void** Slot, const unsigned long* Value)
    {
        if (*Slot != nullptr)
        {
            std::free(*Slot);
            *Slot = nullptr;
        }
        const char* Source = (const char*)(Value[1]);
        if (Source != nullptr)
        {
            const size_t Len = std::strlen(Source) + 1;
            void* Copy = std::malloc(Len);
            if (Copy == nullptr)
                throw std::bad_alloc();
            *Slot = Copy;
            // All four retail bodies reload Value[1] after publishing the
            // allocation. Preserve that order even when Value aliases Slot.
            Source = (const char*)(Value[1]);
            char* Dest = (char*)(Copy);
            char Character;
            do
            {
                Character = *Source++;
                *Dest++ = Character;
            } while (Character != '\0');
        }
    }

    void FlmAssignInterfaceRaw(void** Slot, IDAComponent* NewIface)
    {
        IDAComponent* Old = (IDAComponent*)(*Slot);
        *Slot = NewIface;
        if (NewIface != nullptr)
            NewIface->AddRef();
        if (Old != nullptr)
            Old->Release();
    }

    // Release the old payload when its type changes, then copy by the new type.
    FShadingPropertyValue* FlmAssignTypedValue(FShadingPropertyValue* Dest, const FShadingPropertyValue* Src)
    {
        // Ownership helpers also accept raw tagged DWORD storage.
        const unsigned long NewType = Src->Type;
        if (Dest->Type != NewType)
            FlmReleaseTypedValue((unsigned long*)Dest);
        Dest->Type = NewType;
        switch (NewType)
        {
        case 3:
            Dest->Payload2 = Src->Payload2;
            Dest->Payload1 = Src->Payload1;
            Dest->Payload0 = Src->Payload0;
            break;
        case 4:
            Dest->Payload3 = Src->Payload3;
            Dest->Payload2 = Src->Payload2;
            Dest->Payload1 = Src->Payload1;
            Dest->Payload0 = Src->Payload0;
            break;
        case 1:
        case 2:
        case 8:
        case 9:
        case 10:
            Dest->Payload0 = Src->Payload0;
            break;
        case 5:
        case 7:
            FlmAssignString((unsigned long*)Dest, (const char*)Src->Payload0);
            break;
        case 6:
            FlmAssignInterface((unsigned long*)Dest, (IDAComponent*)Src->Payload0);
            break;
        default:
            break;
        }
        return Dest;
    }

    // Type-nine properties use descriptor-owned sub-blocks.

    // Sub-block setters take the new type and a pointer to the untagged payload.
    void FlmSetDescriptorSubBlockValue(unsigned long* SubBlock, int NewType, const void* ValuePtr)
    {
        const int OldType = (int)(SubBlock[0]);
        if (OldType != NewType)
            FlmReleaseTypedValue(SubBlock);
        SubBlock[0] = (unsigned long)(NewType);
        const int* Values = (const int*)(ValuePtr);
        switch (NewType)
        {
        case 1:
        case 8:
        case 9:
        case 10:
            SubBlock[1] = (unsigned long)(ValuePtr);
            break;
        case 2:
            SubBlock[1] = (unsigned long)(Values[0]);
            break;
        case 3:
            SubBlock[3] = (unsigned long)(Values[2]);
            SubBlock[2] = (unsigned long)(Values[1]);
            SubBlock[1] = (unsigned long)(Values[0]);
            break;
        case 4:
            SubBlock[4] = (unsigned long)(Values[3]);
            SubBlock[3] = (unsigned long)(Values[2]);
            SubBlock[2] = (unsigned long)(Values[1]);
            SubBlock[1] = (unsigned long)(Values[0]);
            break;
        case 5:
        case 7:
            FlmAssignString(SubBlock, (const char*)(ValuePtr));
            break;
        case 6:
            // COM ownership requires a mutable face from this const payload parameter.
            FlmAssignInterface(SubBlock,
                              reinterpret_cast<IDAComponent*>(const_cast<void*>(ValuePtr)));
            break;
        default:
            break;
        }
    }

    // FUN_066f3de0, including fragments LAB_066f3e5e/066f3f09:
    // header stores precede three ordered type-dependent assignments.
    void FlmSetDescriptorEntry(FFlmMaterialParamDesc* Entry, int Type,
                                unsigned long Flags, const char* Tag, const char* Name,
                                const char* Metadata, const void* Value0,
                                const void* Value1, const void* Value2)
    {
        Entry->Path = Flags;
        Entry->ShortTag = Tag;
        Entry->Name = Name;
        Entry->Field0C = Metadata;
        FlmSetDescriptorSubBlockValue(&Entry->Type0, Type, Value0);
        FlmSetDescriptorSubBlockValue(&Entry->Type1, Type, Value1);
        FlmSetDescriptorSubBlockValue(&Entry->Type2, Type, Value2);
    }

    // FUN_066f3cd0: borrowed header clear, then typed resource cleanup in
    // reverse order. RAII keeps earlier subblocks on the retail EH path.
    void FlmResetDescriptorEntry(unsigned long* Entry)
    {
        Entry[0] = 0;
        Entry[1] = 0;
        Entry[2] = 0;
        Entry[3] = 0;
        struct FReleaseSubBlock
        {
            unsigned long* Value;
            ~FReleaseSubBlock() noexcept(false) { FlmReleaseTypedValue(Value); }
        };
        FReleaseSubBlock First{Entry + 0x10 / 4};
        FReleaseSubBlock Second{Entry + 0x24 / 4};
        FReleaseSubBlock Third{Entry + 0x38 / 4};
    }

    // Slot numbers used to live here; the calls below go through ReadFile's published
    // IFileSystem (src/include/readfile.h) instead.

    FFileSystemDesc* FlmBuildFileSystemDesc(FFileSystemDesc* Desc, const char* Path,
                                const char* ComponentName = "FileSystem")
    {
        std::memset(Desc, 0, sizeof(*Desc));
        Desc->Size = 0x34;
        Desc->ComponentName = ComponentName;
        Desc->Path = Path;
        Desc->Access = 0x80000000; // GENERIC_READ
        Desc->ShareMode = 1;       // FILE_SHARE_READ
        Desc->Creation = 3;        // OPEN_EXISTING
        Desc->Flags = 0x08000080;
        Desc->Field30 = 0xFFFFFFFF;
        return Desc;
    }

    unsigned long FsOpen(void* FileSystem, const char* Path)
    {
        FFileSystemDesc Desc;
        FlmBuildFileSystemDesc(&Desc, Path);
        return (unsigned long)(((IFileSystem*)(FileSystem))
            ->OpenByDesc(&Desc)); // +0x7C
    }

    bool FsRead(void* FileSystem, unsigned long Handle, void* Buffer, unsigned long Size)
    {
        unsigned long BytesRead = 0;
        const int Result = ((IFileSystem*)(FileSystem))
                               ->Read(Handle, Buffer, Size, &BytesRead, 0); // +0x14
        return Result != 0 && BytesRead == Size;
    }

    void FsClose(void* FileSystem, unsigned long Handle)
    {
        ((IFileSystem*)(FileSystem))->CloseFile(Handle); // +0x10
    }

    unsigned long FsGetSize(void* FileSystem, unsigned long Handle)
    {
        if (FileSystem == nullptr)
            return 0;
        return ((IFileSystem*)(FileSystem))->GetSize(Handle, 0); // +0x28
    }

    // Load one property by path through the supplied file system.
    bool FlmLoadParameterFromFileSystem(void* FileSystem, const char* Path, unsigned long Type,
                                        void* OutValue)
    {
        if (FileSystem == nullptr || Path == nullptr || OutValue == nullptr)
            return false;

        unsigned long ReadSize = 0;
        switch (Type)
        {
        case 1:
        case 2:
        case 8:
        case 10:
            ReadSize = 4;
            break;
        case 3:
            ReadSize = 0xC;
            break;
        case 4:
            ReadSize = 0x10;
            break;
        default:
            return false; // 5/7 (string) handled below; 9 goes through FlmReadPackedWord / FlmUnpackTwoBitField
        }

        if (Type == 5 || Type == 7)
        {
            const unsigned long Handle = FsOpen(FileSystem, Path);
            if (Handle == (unsigned long)(-1))
                return false;
            const unsigned long Size = FsGetSize(FileSystem, Handle);
            char* Buffer = (char*)(std::malloc((size_t)(Size) + 1));
            bool bOk = false;
            if (Buffer != nullptr)
            {
                bOk = FsRead(FileSystem, Handle, Buffer, Size);
                if (bOk)
                    Buffer[Size] = '\0';
            }
            FsClose(FileSystem, Handle);
            if (!bOk)
            {
                std::free(Buffer);
                return false;
            }
            *(char**)(OutValue) = Buffer; // caller owns; matches slot 18's
                                                            // HeapAlloc'd-string ownership model
            return true;
        }

        const unsigned long Handle = FsOpen(FileSystem, Path);
        if (Handle == (unsigned long)(-1))
            return false;
        const bool bOk = FsRead(FileSystem, Handle, OutValue, ReadSize);
        FsClose(FileSystem, Handle);
        return bOk;
    }

    // FUN_066f30a0: required filesystem; only a successful four-byte read
    // publishes the caller output. The property decoder is a separate function.
    GENRESULT FlmReadPackedWord(IFileSystem* FileSystem, const char* Path,
                                unsigned long* OutValue)
    {
        FFileSystemDesc Desc;
        FlmBuildFileSystemDesc(&Desc, Path);
        const unsigned long Handle = FileSystem->OpenByDesc(&Desc);
        if (Handle == (unsigned long)(-1))
            return GR_GENERIC;
        unsigned long RawValue;
        unsigned long BytesRead = 0;
        const int ReadResult = FileSystem->Read(Handle, &RawValue, 4, &BytesRead, 0);
        const bool bRead = ReadResult != 0 && BytesRead == 4;
        if (bRead)
            *OutValue = RawValue;
        FileSystem->CloseFile(Handle);
        return bRead ? GR_OK : GR_GENERIC;
    }

    unsigned long FlmUnpackTwoBitField(unsigned long Value, int FieldIndex)
    {
        // FUN_066f2470: unknown field indices return zero.
        if (FieldIndex == 0)
            return Value & 3ul;
        if (FieldIndex == 1)
            return (Value >> 2) & 3ul;
        return 0;
    }

    // FUN_066f29b0: field0 deliberately falls through to field1.
    void FlmPackTwoBitField(unsigned long* InOutWord, unsigned long Value, int FieldIndex)
    {
        if (FieldIndex == 0)
        {
            const unsigned long Orig = *InOutWord;
            *InOutWord = Orig ^ ((Orig ^ Value) & 3ul);
        }
        if (FieldIndex == 0 || FieldIndex == 1)
        {
            const unsigned long Orig = *InOutWord;
            const unsigned long Shifted = Value << 2;
            *InOutWord = Orig ^ ((Orig ^ Shifted) & 0xCul);
        }
    }


    // FUN_066f24a0 write descriptors and FUN_066f3160: UTF create/write.
    void FlmBuildFileSystemDescForWrite(FFileSystemDesc* Desc, const char* Path)
    {
        std::memset(Desc, 0, sizeof(*Desc));
        Desc->Size = 0x34;
        Desc->ComponentName = "FileSystem";
        Desc->Path = Path;
        Desc->Implementation = "UTF";
        Desc->Access = 0xC0000000;
        Desc->ShareMode = 1;
        Desc->Creation = 2;
        Desc->Flags = 0x08000080;
        Desc->Field30 = 0xFFFFFFFF;
    }

    unsigned long FsOpenForWrite(void* FileSystem, const char* Path)
    {
        FFileSystemDesc Desc;
        FlmBuildFileSystemDescForWrite(&Desc, Path);
        return (unsigned long)(((IFileSystem*)(FileSystem))
            ->OpenByDesc(&Desc)); // +0x7C
    }

    bool FsWrite(void* FileSystem, unsigned long Handle, const void* Buffer, unsigned long Size)
    {
        unsigned long BytesWritten = 0;
        // +0x18: typed source, byte-count output, optional position override.
        const int Result = ((IFileSystem*)(FileSystem))
                               ->Write(Handle, Buffer, Size, &BytesWritten, nullptr);
        return Result != 0 && BytesWritten == Size;
    }

    // FUN_066f3160: UTF create/write descriptor; the third argument is
    // the caller's four-byte buffer, passed unchanged through Write.
    unsigned long FlmWriteSmallConfigValue(void* FileSystem, const char* Path,
                                            const unsigned long* Value)
    {
        const unsigned long Handle = FsOpenForWrite(FileSystem, Path);
        if (Handle == (unsigned long)(-1))
            return 0x80004005ul;
        const bool bOk = FsWrite(FileSystem, Handle, Value, 4);
        FsClose(FileSystem, Handle);
        return bOk ? 0ul : 0x80004005ul;
    }

    // Keep the copied property value alive through serialization and unwind cleanup.
    struct FFlmScopedTypedValue
    {
        unsigned long Words[5] = {};
        ~FFlmScopedTypedValue() noexcept(false) { FlmReleaseTypedValue(Words); }
    };

    // FUN_066f1fc0: typed load result is committed through Slot26 only
    // after successful I/O. Type6/default cases still commit their zero value.
    bool FlmLoadDeclaredParameter(FMaterialImpl* Material, void* FileSystem,
                                  unsigned char* Entry, unsigned long Index)
    {
        FFlmScopedTypedValue Value;
        const unsigned long Type = *(const unsigned long*)(Entry + 0x10);
        Value.Words[0] = Type;
        if (Type == 9)
        {
            const char* Path = *(const char* const*)(Entry + 4);
            unsigned long RawValue;
            if (FlmReadPackedWord((IFileSystem*)FileSystem, Path, &RawValue) < 0)
                return false;
            const unsigned long Flags = *(const unsigned long*)Entry;
            const int FieldIndex = (Flags & 0x20000) == 0 && (Flags & 0x40000) != 0 ? 1 : 0;
            Value.Words[1] = FlmUnpackTwoBitField(RawValue, FieldIndex);
            Material->StoreParameterValue(Index, Entry, Value.Words);
            return true;
        }
        if (Type >= 1 && Type <= 8 && Type != 6)
        {
            const char* Path = *(const char* const*)(Entry + 4);
            FFileSystemDesc Desc;
            FlmBuildFileSystemDesc(&Desc, Path);
            IFileSystem* Fs = (IFileSystem*)FileSystem;
            const unsigned long Handle = Fs->OpenByDesc(&Desc);
            if (Handle == (unsigned long)-1)
                return false;
            if (Type == 5 || Type == 7)
            {
                const unsigned long Size = Fs->GetSize(Handle, 0);
                char* Buffer = new (std::nothrow) char[Size];
                if (Buffer == nullptr)
                {
                    Fs->CloseFile(Handle);
                    return false;
                }
                const bool bOk = FsRead(Fs, Handle, Buffer, Size);
                Fs->CloseFile(Handle);
                if (!bOk)
                    return false; // Retail 066f2272 branches before buffer delete.
                FlmAssignString(Value.Words, Buffer);
                delete[] Buffer;
            }
            else
            {
                const unsigned long Size = Type == 3 ? 12ul : Type == 4 ? 16ul : 4ul;
                const bool bOk = FsRead(Fs, Handle, Value.Words + 1, Size);
                Fs->CloseFile(Handle);
                if (!bOk)
                    return false;
                if (Type == 8)
                    Value.Words[1] = (Value.Words[1] >> 4) & 0xful;
            }
        }
        Material->StoreParameterValue(Index, Entry, Value.Words);
        return true;
    }

    bool FlmSaveParameterToFileSystem(FMaterialImpl* Material, void* FileSystem,
                                      const unsigned char* Entry, unsigned long Index)
    {
        FFlmScopedTypedValue Value;
        const unsigned long Type = *(const unsigned long*)(Entry + 0x10);
        Value.Words[0] = Type;
        const unsigned long* Raw = (const unsigned long*)(Material->GetParameterAddress(Index));
        if (Raw == nullptr)
            return false;
        switch (Type)
        {
        case 4: Value.Words[4] = Raw[3]; [[fallthrough]];
        case 3:
            Value.Words[3] = Raw[2];
            Value.Words[2] = Raw[1];
            [[fallthrough]];
        case 1: case 2: case 8: case 9: case 10:
            Value.Words[1] = Raw[0];
            break;
        case 5: case 7:
            FlmAssignString(Value.Words, (const char*)(Raw[0]));
            break;
        case 6:
            FlmAssignInterface(Value.Words, (IDAComponent*)(Raw[0]));
            break;
        default:
            break;
        }
        const char* Path = *(const char* const*)(Entry + 4);
        if (Type == 6 || Type == 0 || Type > 9)
            return true;
        if (Type == 8)
        {
            unsigned long Packed = 0;
            const unsigned long Handle = FsOpen(FileSystem, Path);
            if (Handle != (unsigned long)(-1))
            {
                unsigned long ReadValue;
                if (FsRead(FileSystem, Handle, &ReadValue, 4))
                    Packed = ReadValue;
                FsClose(FileSystem, Handle);
            }
            Packed ^= (Packed ^ (Value.Words[1] << 4)) & 0xf0ul;
            // 066f28cb / 066f2924 reread the path after filesystem callbacks.
            const char* WritePath = *(const char* const*)(Entry + 4);
            return (long)(FlmWriteSmallConfigValue(FileSystem, WritePath, &Packed)) >= 0;
        }
        if (Type == 9)
        {
            unsigned long Packed = 0;
            FlmReadPackedWord((IFileSystem*)(FileSystem), Path, &Packed);
            const unsigned long Flags = *(const unsigned long*)(Entry);
            const int FieldIndex = (Flags & 0x20000) == 0 && (Flags & 0x40000) != 0 ? 1 : 0;
            FlmPackTwoBitField(&Packed, Value.Words[1], FieldIndex);
            // 066f28cb / 066f2924 reread the path after filesystem callbacks.
            const char* WritePath = *(const char* const*)(Entry + 4);
            return (long)(FlmWriteSmallConfigValue(FileSystem, WritePath, &Packed)) >= 0;
        }
        const void* Buffer = Value.Words + 1;
        unsigned long Size = 4;
        if (Type == 3)
            Size = 12;
        else if (Type == 4)
            Size = 16;
        else if (Type == 5 || Type == 7)
        {
            Buffer = (const char*)(Value.Words[1]);
            Size = (unsigned long)(std::strlen((const char*)(Buffer))) + 1;
        }
        const unsigned long Handle = FsOpenForWrite(FileSystem, Path);
        if (Handle == (unsigned long)(-1))
            return false;
        const bool bOk = FsWrite(FileSystem, Handle, Buffer, Size);
        FsClose(FileSystem, Handle);
        return bOk;
    }

    void DropQiRef(IDAComponent* Object)
    {
        if (Object != nullptr)
            Object->Release();
    }

    // The retail material binds hold the QI reference in an EH-unwound smart
    // pointer. Own the out-param before QI so an exception also releases it.
    struct FScopedComponentReference
    {
        void* Object = nullptr;
        ~FScopedComponentReference() noexcept(false)
        {
            if (Object != nullptr)
            {
                ((IDAComponent*)(Object))->Release();
                Object = nullptr;
            }
        }
    };

    // DAT_06704060 / 067022C4 / 067022C8 / 06701394 — FUN_066F8C50.
    const float kNomadPeriod = 5.0f;
    const float kNomadColorScale = 255.0f;
    const double kNomadHalf = 0.5; // double DAT_067022c8
    const unsigned long kNomadColorLo = 0x000AEAF5;
    const unsigned long kNomadColorHi = 0x000AFFE6;
    I3DMathEngine* G_NomadMath = nullptr;

    struct FNomadContext
    {
        void* Inst;
        float* World;
        float* View;
        const FRpVertexSrcDesc* Mesh;
        unsigned short* Indices;
        FVbListing* VbListing;
        void* IbWrapper;
        // Borrowed candidate material used for inherited opacity.
        void* Candidate;
    };

    int FvfXyzSize(unsigned long Fvf)
    {
        if ((Fvf & 0xC) == 0xC)
            return 0x1C;
        if ((Fvf & 4) == 4)
            return 0x10;
        if ((Fvf & 2) == 2)
            return 0xC;
        return 0;
    }

    int FvfColorSize(unsigned long Fvf)
    {
        int Size = 0;
        if ((Fvf & 0x40) != 0)
            Size += 4;
        if ((Fvf & 0x80) != 0)
            Size += 4;
        return Size;
    }

    int FvfNormalSize(unsigned long Fvf)
    {
        return ((Fvf & 0x10) != 0) ? 0xC : 0;
    }

    int FvfTexSize(unsigned long Fvf)
    {
        int Size = 0;
        unsigned long Count = (Fvf >> 8) & 0xF;
        unsigned long Shift = 16;
        while (Count != 0)
        {
            const unsigned long Lane = (Fvf >> (Shift & 31)) & 3;
            if (Lane == 0)
                Size += 8;
            else if (Lane == 1)
                Size += 0xC;
            else if (Lane == 2)
                Size += 0x10;
            else
                Size += 4;
            Shift += 2;
            Count -= 1;
        }
        return Size;
    }

    int FvfStride(unsigned long Fvf)
    {
        return FvfXyzSize(Fvf) + FvfNormalSize(Fvf) + FvfColorSize(Fvf) + FvfTexSize(Fvf);
    }

    unsigned long NomadLockFvf(unsigned long Stage)
    {
        if ((long)(Stage) > 1)
            return 0x242;
        if ((long)(Stage) > 0)
            return 0x142;
        return 0x42;
    }

    double NomadFadeFromValue(float Value)
    {
        // 066f8ccd stores float but keeps the original subtraction on the FPU stack.
        const double WideDelta = 1.0 - (double)(Value);
        const float Delta = (float)(WideDelta);
        double Curve;
        // TEST AH,05 + JP: negative falls through to zero; positive/equal/unordered jump.
        if (WideDelta < 0.0)
            Curve = 0.0;
        // TEST AH,01 + JNZ: less/unordered take the polynomial; >=1 takes one.
        else if (Delta >= 1.0f)
            Curve = 1.0;
        else
            Curve = ((3.0 - 2.0 * (double)(Delta)) * Delta) * Delta;
        return 1.0 - Curve;
    }

    char* NomadLockDest(const FVertexMemoryLock* LockOut)
    {
        return (char*)(LockOut->Vertices);
    }

    GENRESULT VbMgrLock(IVertexBufferManager* VbMgr, unsigned long Fvf, unsigned long Count, FVertexMemoryLock* Out)
    {
        if (VbMgr == nullptr)
            return GR_GENERIC;
        return VbMgr->LockVertexMemory(Fvf, Count, Out);
    }

    void VbMgrUnlock(IVertexBufferManager* VbMgr, FVertexMemoryLock* Out)
    {
        if (VbMgr == nullptr)
            return;
        VbMgr->UnlockVertexMemory(Out);
    }

    GENRESULT RpDrawIndexed(IRPDraw* RpDraw, unsigned long Prim, unsigned long Start, unsigned long Count,
                            const void* Indices, unsigned long IndexSpan)
    {
        return RpDraw->DrawIndexedPrimitive(
            Prim, (unsigned int)(Start), (unsigned int)(Count),
            (unsigned int)((unsigned long)(Indices)),
            (unsigned int)(IndexSpan));
    }

    // The manager copies mesh channels into the destination using the requested FVF.
    void VbMgrFillVertexMemory(IVertexBufferManager* VbMgr, void* Dest, unsigned long Fvf, const FRpVertexSrcDesc* Mesh,
                               unsigned long Start, unsigned long Count)
    {
        if (VbMgr == nullptr)
            return;
        VbMgr->CopyVertexData(
            Dest, Fvf, Mesh,
            Start, Count);
    }

    void NomadComposeViewWorld(const float* World, const float* View, float* Out) // FUN_066f9500
    {
        if (G_NomadMath == nullptr)
            G_NomadMath = FlmCreateMathEngine();
        FTransform Combined;
        G_NomadMath->MulTransform(Combined, *(const FTransform*)(View),
            *(const FTransform*)(World));
        std::memcpy(Out, &Combined, sizeof(Combined));
    }

    void NomadInverseLight(const float* World, const float* View, float* OutLight)
    {
        float Combined[12];
        NomadComposeViewWorld(World, View, Combined);
        // 066f901b..066f9041 re-read the cache after MulTransform, independently of its helper.
        if (G_NomadMath == nullptr)
            G_NomadMath = FlmCreateMathEngine();
        const FVector MinusZ = {0.0f, 0.0f, -1.0f};
        G_NomadMath->InverseRotate(*(FVector*)(OutLight),
            *(const FTransform*)(Combined), MinusZ);
    }

    // LAB_066f95b0 / FUN_066f95c0. The two default texture cursors start with stride 8.
    struct FNomadCursor
    {
        char* Pointer;
        unsigned long Stride;

        FNomadCursor() : Pointer(nullptr), Stride(8) {}
        FNomadCursor(char* InPointer, unsigned long InStride)
            : Pointer(InPointer), Stride(InStride) {}
        ~FNomadCursor()
        {
            Pointer = nullptr;
            Stride = 0;
        }
    };

    void NomadWriteVert(FNomadCursor& PositionOut, FNomadCursor& ColorOut,
                        FNomadCursor* TextureOut, unsigned long Index,
                        const unsigned long& Stage, unsigned long Color, float Phase,
                        const float* Position, const float* Normal, const float* Light,
                        const unsigned char* UvBase, const unsigned long* UvIndex, unsigned long UvStride, bool bIndexed)
    {
        // DWORD copies preserve all position/UV bit patterns; neither path copies normals.
        const unsigned long* PositionWords = (const unsigned long*)(Position);
        unsigned long* DestPosition = (unsigned long*)(PositionOut.Pointer);
        DestPosition[0] = PositionWords[0];
        DestPosition[1] = PositionWords[1];
        DestPosition[2] = PositionWords[2];
        const float NormalX = Normal[0];
        const float NormalY = Normal[1];
        const float NormalZ = Normal[2];
        *(unsigned long*)(ColorOut.Pointer) = Color;
        // 066f9140..9181 / 066f92de..9326: Z*Z + Y*Y, then X*X, +1, double .5.
        const double Zy = (double)(Light[2]) * NormalZ +
                          (double)(Light[1]) * NormalY;
        const double Lit = ((Zy + (double)(Light[0]) * NormalX) + 1.0) * kNomadHalf;
        if ((long)(Stage) > 1)
        {
            const unsigned long UvOffset = bIndexed ? UvIndex[Index] * UvStride : Index * UvStride;
            const unsigned long* SourceUv = (const unsigned long*)(UvBase + UvOffset);
            unsigned long* DestUv = (unsigned long*)(TextureOut[0].Pointer + Index * TextureOut[0].Stride);
            DestUv[0] = SourceUv[0];
            DestUv[1] = SourceUv[1];
        }
        // Re-read the owner's field as in both loops; generated coordinates occupy the last lane.
        const long GeneratedStage = (long)(Stage);
        if (GeneratedStage > 0)
        {
            FNomadCursor& Generated = TextureOut[GeneratedStage - 1];
            float* Uv = (float*)(Generated.Pointer + Index * Generated.Stride);
            Uv[0] = (float)(Lit);
            Uv[1] = Phase;
        }
        PositionOut.Pointer += PositionOut.Stride;
        ColorOut.Pointer += ColorOut.Stride;
    }

    void NomadCopyMesh(const FRpVertexSrcDesc* Mesh, unsigned long Start, unsigned long Count,
                       FNomadCursor& PositionOut, FNomadCursor& ColorOut, FNomadCursor* TextureOut,
                       const unsigned long& Stage, unsigned long Color, float Phase, const float* Light)
    {
        if ((Mesh->Flags & 4) != 0)
        {
            const unsigned long PosStride = Mesh->Position.Stride;
            const unsigned char* PosBase = Mesh->Position.Base;
            const unsigned long NrmStride = Mesh->Normal.Stride;
            const unsigned char* NrmBase = Mesh->Normal.Base;
            const unsigned long* PosIndex = Mesh->Position.IndexArray + Start;
            const unsigned long* NrmIndex = Mesh->Normal.IndexArray + Start;
            const unsigned long* UvIndex = Mesh->Tex[0].IndexArray + Start;
            const unsigned long UvStride = Mesh->Tex[0].Stride;
            const unsigned char* UvBase = Mesh->Tex[0].Base;
            for (unsigned long Index = 0; Index < Count; ++Index)
            {
                const float* Position = (const float*)(PosBase + PosIndex[Index] * PosStride);
                const float* Normal = (const float*)(NrmBase + NrmIndex[Index] * NrmStride);
                NomadWriteVert(PositionOut, ColorOut, TextureOut, Index, Stage, Color, Phase,
                               Position, Normal, Light, UvBase, UvIndex, UvStride, true);
            }
            return;
        }

        const unsigned long SrcFvf = Mesh->SrcFvf;
        const unsigned long SrcStride = (unsigned long)(FvfStride(SrcFvf));
        const unsigned long Offset = Start * SrcStride;
        const unsigned char* PosBase = Mesh->Position.Base + Offset;
        const unsigned char* NrmBase = Mesh->Normal.Base + Offset;
        const unsigned char* UvBase = Mesh->Tex[0].Base + Offset;
        for (unsigned long Index = 0; Index < Count; ++Index)
        {
            const float* Position = (const float*)(PosBase + Index * SrcStride);
            const float* Normal = (const float*)(NrmBase + Index * SrcStride);
            NomadWriteVert(PositionOut, ColorOut, TextureOut, Index, Stage, Color, Phase,
                           Position, Normal, Light, UvBase, nullptr, SrcStride, false);
        }
    }

    // Each state becomes armed after its Set call: unwind states 3 (lighting), 4 (zwrite).
    struct FNomadRenderStateRestore
    {
        IRenderPipeline* Pipeline;
        D3DRENDERSTATETYPE State;
        int SavedValue;
        bool bActive;

        FNomadRenderStateRestore(IRenderPipeline* InPipeline, D3DRENDERSTATETYPE InState)
            : Pipeline(InPipeline), State(InState), bActive(false)
        {
            Pipeline->GetRenderState(State, &SavedValue);
        }
        void Restore()
        {
            bActive = false;
            Pipeline->SetRenderState(State, SavedValue);
        }
        ~FNomadRenderStateRestore() noexcept(false)
        {
            if (bActive)
                Restore();
        }
    };

    // Resolve the root instance before looking up its fade value.
    float FlmRootInstanceValue(void* Context, float Default)
    {
        if (Context == nullptr)
            return Default;

        const int InstanceId =
            (int)((intptr_t)(*(void**)(Context)));
        int RootId = InstanceId;
        if (G_FlmEngineOuter != nullptr)
        {
            void* Root = ((IEngine*)(G_FlmEngineOuter))->GetInstanceRoot(
                (void*)((intptr_t)(InstanceId)));
            RootId = (int)((intptr_t)(Root));
        }

        void* Found = nullptr;
        if (!FlmLookupIdValue(RootId, &Found))
            return Default;

        float Value = Default;
        const unsigned long Bits = (unsigned long)((intptr_t)(Found));
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }

    // 066f8c84..066f8cbd: preserve the engine captured before root lookup;
    // Context-null skips this prelude, but a nonnull Context always calls slot49.
    float NomadFadeLookup(void* Context)
    {
        IEngine* Engine = (IEngine*)(G_FlmEngineOuter);
        const float Value = FlmRootInstanceValue(Context, 1.0f);
        if (Context != nullptr)
        {
            FNomadContext* Raw = (FNomadContext*)(Context);
            Engine->GetInstanceRelative(Raw->Inst);
        }
        return Value;
    }

    GENRESULT SubmitNomadGeometry(const unsigned char& bSecondTexture, const float& Live6C,
                          const unsigned long& Stage, IVertexBufferManager* const& VbMgr,
                          IRenderPipeline* const& Pipeline, void* Context,
                          unsigned long Prim, unsigned long Start, unsigned long Count,
                          unsigned long IbStart, unsigned long IndexSpan)
    {
        const double Fade = NomadFadeFromValue(NomadFadeLookup(Context));
        FNomadContext* Submit = (FNomadContext*)(Context);
        unsigned long Color;
        float Phase;
        if (bSecondTexture == 0)
        {
            const int Byte = (int)((Fade * kNomadHalf) * kNomadColorScale);
            Color = ((unsigned long)(Byte) << 24) |
                    ((long)(Stage) > 0 ? kNomadColorHi : kNomadColorLo);
            Phase = 1.0f;
        }
        else
        {
            const int Byte = (int)(Fade * kNomadColorScale);
            Color = ((unsigned long)(Byte) << 24) | 0x00ffffff;
            // _CIfmod receives ST1=Live6C, ST0=period; division stays wide until FSTP.
            Phase = (float)(1.0 - std::fmod((double)(Live6C),
                (double)(kNomadPeriod)) / kNomadPeriod);
        }

        FVertexMemoryLock LockOut;
        if (VbMgr->LockVertexMemory(
                NomadLockFvf(Stage), Count, &LockOut) < 0)
            return (GENRESULT)(-1);

        const unsigned long Fvf = LockOut.Listing.Fvf;
        const unsigned long Stride = (unsigned long)(FvfStride(Fvf));
        const unsigned long ColorOffset = (unsigned long)(FvfXyzSize(Fvf) + FvfNormalSize(Fvf));
        const unsigned long TextureOffset = ColorOffset + (unsigned long)(FvfColorSize(Fvf));
        char* Vertices = NomadLockDest(&LockOut);
        FNomadCursor PositionOut(Vertices, Stride);
        FNomadCursor ColorOut(Vertices + ColorOffset, Stride);
        FNomadCursor TextureOut[2];
        TextureOut[0].Pointer = Vertices + TextureOffset;
        TextureOut[0].Stride = Stride;
        TextureOut[1].Pointer = Vertices + TextureOffset + 8;
        TextureOut[1].Stride = Stride;
        float Light[3];
        NomadInverseLight(Submit->World, Submit->View, Light);
        NomadCopyMesh(Submit->Mesh, Start, Count, PositionOut, ColorOut, TextureOut, Stage, Color, Phase, Light);
        VbMgr->UnlockVertexMemory(&LockOut);

        FNomadRenderStateRestore DepthWrite(Pipeline, D3DRS_ZWRITEENABLE); // formerly Lighting
        DepthWrite.Pipeline->SetRenderState(DepthWrite.State, 0);
        DepthWrite.bActive = true;
        FNomadRenderStateRestore CullMode(Pipeline, D3DRS_CULLMODE); // formerly ZWrite
        CullMode.Pipeline->SetRenderState(CullMode.State, 1);
        CullMode.bActive = true;
        Pipeline->DrawIndexedPrimitiveVb(Prim, LockOut.Listing.VertexBuffer, LockOut.Listing.BaseVertex,
                                        Count, Submit->Indices + IbStart, IndexSpan, 0);
        CullMode.Restore();
        DepthWrite.Restore();
        return GR_OK;
    }

    // Lock, copy through the vertex manager, unlock, then draw with the granted buffer offset.
    GENRESULT FlmFillMeshAndDraw(IRenderPipeline* Pipeline, IVertexBufferManager* VbMgr, void* Context,
                                unsigned long Prim, unsigned long Start, unsigned long Count,
                                unsigned long IbStart, unsigned long IndexSpan, unsigned long /*Flags*/)
    {
        // 066f60d0..622a: all pointers required, and lock/draw results pass through.
        FNomadContext* Submit = (FNomadContext*)(Context);
        IVertexBufferManager* Manager = VbMgr;
        const unsigned long RequestedFvf =
            Submit->Mesh->SrcFvf;
        FVertexMemoryLock LockOut;
        const GENRESULT Result = Manager->LockVertexMemory(RequestedFvf, Count, &LockOut);
        if (Result < 0)
            return Result;

        const unsigned long GrantedFvf = LockOut.Listing.Fvf;
        unsigned long TextureBytes = 0;
        unsigned long TextureCount = (GrantedFvf >> 8) & 0xf;
        unsigned long Shift = 16;
        while (TextureCount != 0)
        {
            // 066f6182 SHR EAX,CL masks the shift to five bits on x86.
            const unsigned long Lane = (GrantedFvf >> (Shift & 31)) & 3;
            if (Lane == 0)
                TextureBytes += 8;
            else if (Lane == 1)
                TextureBytes += 12;
            else if (Lane == 2)
                TextureBytes += 16;
            else
                TextureBytes += 4;
            Shift += 2;
            --TextureCount;
        }
        const unsigned long Stride = (unsigned long)(FvfXyzSize(GrantedFvf) +
            FvfNormalSize(GrantedFvf) + FvfColorSize(GrantedFvf)) + TextureBytes;
        void* DestBase = (void*)((uintptr_t)(LockOut.Vertices) -
            Stride * Start);
        Manager->CopyVertexData(DestBase, GrantedFvf, Submit->Mesh,
            Start, Count);
        Manager->UnlockVertexMemory(&LockOut);
        void* Indices = (void*)((uintptr_t)(Submit->Indices) +
            IbStart * 2);
        return Pipeline->DrawIndexedPrimitiveVb(Prim, LockOut.Listing.VertexBuffer, LockOut.Listing.BaseVertex,
            Count, Indices, IndexSpan, 0);
    }

    // Try the direct draw and existing vertex listing before copying vertices.
    GENRESULT FlmMeshDrawPass(IRenderPipeline* Pipeline, IRPDraw* RpDraw, IVertexBufferManager* VbMgr, void* Context,
                              unsigned long Prim, unsigned long Start, unsigned long Count,
                              unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags)
    {
        FNomadContext* Submit = (FNomadContext*)(Context);
        if ((Flags & 0x100) != 0)
        {
            const uintptr_t IbBase = *(const uintptr_t*)(
                (const unsigned char*)(Submit->IbWrapper) + 4);
            const GENRESULT Result = RpDrawIndexed(RpDraw, Prim, Start, Count,
                (const void*)(IbBase + IbStart), IndexSpan);
            if (Result >= 0)
                return Result;
        }

        // Every listing explicitly discards a failed RpDraw result here.
        GENRESULT Result = (GENRESULT)(-1);
        if (Submit->VbListing != nullptr)
        {
            const FVbListing* Listing = Submit->VbListing;
            Result = Pipeline->DrawIndexedPrimitiveVb(Prim, Listing->VertexBuffer,
                Listing->BaseVertex + Start, Count,
                (void*)((uintptr_t)(Submit->Indices) + IbStart * 2),
                IndexSpan, 0);
        }
        if (Result >= 0 || Submit->Mesh == nullptr || Count == 0 || IndexSpan == 0)
            return Result;
        return FlmFillMeshAndDraw(Pipeline, VbMgr, Context, Prim, Start, Count, IbStart,
                                 IndexSpan, Flags);
    }

    // Restore the shared material render-state table after the draw.
    const unsigned long kEnvGlassDrawTailRs[] = {0x89, 1, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};

    // FUN_066f3080: restore the saved value through the stored pipeline.
    // Active marks the retail EH lifetime, not whether GetRenderState succeeded.
    struct FFlmRenderStateRestore
    {
        IRenderPipeline* Pipeline;
        D3DRENDERSTATETYPE State;
        int SavedValue;
        bool bActive;

        FFlmRenderStateRestore(IRenderPipeline* InPipeline, D3DRENDERSTATETYPE InState)
            : Pipeline(InPipeline), State(InState), bActive(false)
        {
            Pipeline->GetRenderState(State, &SavedValue);
        }
        void Restore()
        {
            bActive = false;
            Pipeline->SetRenderState(State, SavedValue);
        }
        ~FFlmRenderStateRestore() noexcept(false)
        {
            if (bActive)
                Restore();
        }
    };

    // FUN_066f9ea0: normal and EH paths restore 0x17 before 0x0e.
    GENRESULT SubmitPlanetWaterGeometry(IRenderPipeline* Pipeline, IRPDraw* RpDraw, IVertexBufferManager* VbMgr, void* Context,
                                unsigned long Prim, unsigned long Start, unsigned long Count,
                                unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags)
    {
        FFlmRenderStateRestore DepthWrite(Pipeline, D3DRS_ZWRITEENABLE); // formerly Lighting
        Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        DepthWrite.bActive = true;
        FFlmRenderStateRestore DepthFunction(Pipeline, D3DRS_ZFUNC); // formerly Cull
        Pipeline->SetRenderState(D3DRS_ZFUNC, 4);
        DepthFunction.bActive = true;
        const GENRESULT Result = FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start,
                                                Count, IbStart, IndexSpan, Flags);
        DepthFunction.Restore();
        DepthWrite.Restore();
        return Result;
    }

    // LAB_066f3370: same draw core, with inline granted-FVF fill in retail.
    // FlmFillMeshAndDraw preserves that complete calculation and call sequence.
    GENRESULT SubmitRpDrawGeometry(IRPDraw* RpDraw, IRenderPipeline* Pipeline, IVertexBufferManager* VbMgr, void* Context,
                                 unsigned long Prim, unsigned long Start, unsigned long Count,
                                 unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags)
    {
        return FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start, Count, IbStart,
                              IndexSpan, Flags);
    }

    // LAB_06700260: both initial gates return zero before reading Context.
    // Enabled HUDAnim then uses the complete shared granted-FVF draw sequence.
    GENRESULT SubmitHudAnimatedGeometry(unsigned long HudAnimFlag, IRPDraw* RpDraw, IRenderPipeline* Pipeline,
                            IVertexBufferManager* VbMgr, void* Context, unsigned long Prim, unsigned long Start,
                            unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                            unsigned long Flags)
    {
        if (HudAnimFlag == 0 || (Flags & 0x100) == 0)
            return GR_OK;
        return FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start, Count, IbStart,
                              IndexSpan, Flags);
    }

    // Wire drawing queries the shared IMesh and IVMeshWire interfaces.

    // Reject near-degenerate edges; Wire2 offsets duplicate points by the half-width.
    const double kWireEdgeDegenerateThreshold = 0.1;
    const float kWireEdgeQuadHalfOffset = 0.005f;

    // File-local stat counters for the two IMesh edge-draw paths below (DAT_067045c8/cc for
    // Wire's own fallback -- 26924/26920; DAT_067045d4/d8 for Wire2 -- 28307/28303). No
    // confirmed external reader anywhere in this DLL, so kept file-local rather than
    // invented as part of the public FMaterialImpl/FLAppearance surface.
    unsigned long G_WireLockFailCount = 0;
    unsigned long G_WireDrawAttemptCount = 0;
    unsigned long G_Wire2LockFailCount = 0;
    unsigned long G_Wire2DrawAttemptCount = 0;

    // Both listings: select current group, retry zero on any nonzero result.
    // The caller owns the QI pointer before the call, including negative QI outputs.
    bool MeshSelectAndAdvanceCursor(IMesh* Mesh, unsigned long* Cursor)
    {
        if (Mesh->OpenFaceGroup((int)(*Cursor), 1) != 0)
        {
            *Cursor = 0;
            if (Mesh->OpenFaceGroup(0, 1) < 0)
                return false;
        }
        ++*Cursor;
        return true;
    }

    struct FMeshEdgeArrays
    {
        float* Values;
        unsigned short* ValueIndices;
        float* Positions;
        unsigned short* PositionIndices;
        unsigned long IndexCount;
    };

    // Fetch color/value and position channel arrays and their independent indices.
    void MeshFetchEdgeArrays(IMesh* Mesh, FMeshEdgeArrays& Arrays)
    {
        void* UnusedB;
        void* UnusedC;
        void* UnusedD;
        void* UnusedCount;
        void* IndexCount;
        Mesh->GetFaceGroupData(0x100, (void**)(&Arrays.Values),
            &UnusedB, &UnusedC, &UnusedD);
        Mesh->GetFaceGroupIndices(0x100, (void**)(&Arrays.ValueIndices), &UnusedCount);
        Mesh->GetFaceGroupData(0x80, (void**)(&Arrays.Positions),
            &UnusedB, &UnusedC, &UnusedD);
        Mesh->GetFaceGroupIndices(0x80, (void**)(&Arrays.PositionIndices), &IndexCount);
        Arrays.IndexCount = (unsigned long)((uintptr_t)(IndexCount));
    }

    void MeshFinalizeIteration(IMesh* Mesh)
    {
        Mesh->UnlockFaceGroupChannels();
        Mesh->CloseFaceGroupChannels();
        Mesh->CloseFaceGroup();
    }

    // Slot18 is a status, only -8 has a special branch. Count comes from Slot24(80).
    // Each accepted edge emits two vertices in 066fb540, twelve in 066fc150.
    GENRESULT WireDrawEdges(IRenderPipeline* Pipeline, IVertexBufferManager* VertexBufferManager,
                           IMesh* Mesh, const unsigned long& Color0, const unsigned long& Color1, bool bWire2)
    {
        if (Mesh->OpenFaceGroupChannels(0x180) == -8)
        {
            Mesh->CloseFaceGroup();
            return GR_OK;
        }
        Mesh->LockFaceGroupChannels(0x180);
        FMeshEdgeArrays Arrays;
        MeshFetchEdgeArrays(Mesh, Arrays);
        FVertexMemoryLock LockOut;
        IVertexBufferManager* Manager = VertexBufferManager;
        const unsigned long ReservationCount = bWire2 ? Arrays.IndexCount * 6 : Arrays.IndexCount;
        if (Manager->LockVertexMemory(0x42, ReservationCount, &LockOut) >= 0)
        {
            const unsigned long Stride = (unsigned long)(FvfStride(LockOut.Listing.Fvf));
            const unsigned long ColorOffset = (unsigned long)(
                FvfXyzSize(LockOut.Listing.Fvf) + FvfNormalSize(LockOut.Listing.Fvf));
            unsigned char* Dest = (unsigned char*)(LockOut.Vertices);
            unsigned long ValidVertexCount = 0;
            for (unsigned long Edge = 0; Edge < (Arrays.IndexCount >> 1); ++Edge)
            {
                // FCOMP double(0.1); TEST AH,41; JNP exits for less/equal.
                // Unordered has TWO set mask bits: even parity, so it falls through.
                if ((double)(Arrays.Values[Arrays.ValueIndices[Edge]]) <=
                    kWireEdgeDegenerateThreshold)
                    break;
                if (!bWire2)
                {
                    for (unsigned long Endpoint = 0; Endpoint < 2; ++Endpoint)
                    {
                        const unsigned short Index = Arrays.PositionIndices[Edge * 2 + Endpoint];
                        std::memcpy(Dest, Arrays.Positions + (unsigned long)(Index) * 3,
                            3 * sizeof(float));
                        *(unsigned long*)(Dest + ColorOffset) = Color0;
                        Dest += Stride;
                        ++ValidVertexCount;
                    }
                }
                else
                {
                    // Emit four triangles from original and offset endpoints.
                    const unsigned long Endpoints[12] = {0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1};
                    const unsigned char ColorSelectors[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1};
                    const signed char Offsets[12] = {0, 0, -1, 0, -1, -1, 0, 0, 1, 0, 1, 1};
                    for (unsigned long Slot = 0; Slot < 12; ++Slot)
                    {
                        const unsigned short Index = Arrays.PositionIndices[Edge * 2 + Endpoints[Slot]];
                        std::memcpy(Dest, Arrays.Positions + (unsigned long)(Index) * 3,
                            3 * sizeof(float));
                        *(unsigned long*)(Dest + ColorOffset) =
                            ColorSelectors[Slot] == 0 ? Color0 : Color1;
                        if (Offsets[Slot] != 0)
                        {
                            float* Position = (float*)(Dest);
                            const double Offset = (double)(kWireEdgeQuadHalfOffset);
                            if (Offsets[Slot] < 0)
                            {
                                Position[0] = (float)((double)(Position[0]) - Offset);
                                Position[1] = (float)((double)(Position[1]) - Offset);
                            }
                            else
                            {
                                Position[0] = (float)((double)(Position[0]) + Offset);
                                Position[1] = (float)((double)(Position[1]) + Offset);
                            }
                        }
                        Dest += Stride;
                        ++ValidVertexCount;
                    }
                }
            }
            Manager->UnlockVertexMemory(&LockOut);
            if (ValidVertexCount > 0)
                PipelineDrawPrimitiveVb(Pipeline, bWire2 ? 4 : 2, LockOut.Listing.VertexBuffer,
                    LockOut.Listing.BaseVertex, ValidVertexCount, nullptr);
            if (bWire2)
                ++G_Wire2DrawAttemptCount;
            else
                ++G_WireDrawAttemptCount;
        }
        else if (bWire2)
        {
            ++G_Wire2LockFailCount;
        }
        else
        {
            ++G_WireLockFailCount;
        }
        MeshFinalizeIteration(Mesh);
        return GR_OK;
    }

    struct FScopedWireRenderState
    {
        IRenderPipeline* Pipeline;
        D3DRENDERSTATETYPE State;
        unsigned long Value;
        FScopedWireRenderState(IRenderPipeline* InPipeline, D3DRENDERSTATETYPE InState, unsigned long InValue)
            : Pipeline(InPipeline), State(InState)
        {
            PipelineGetRenderState(Pipeline, State, &Value);
            PipelineSetRenderState(Pipeline, State, InValue);
        }
        ~FScopedWireRenderState() noexcept(false)
        {
            PipelineSetRenderState(Pipeline, State, Value);
        }
    };

    // 066fb540..ba20: mandatory Context, optional mesh/count/engine gates.
    // QI owners and state savers unwind in reverse construction order.
    GENRESULT SubmitWireGeometry(const unsigned long& WireColor, IRenderPipeline* Pipeline, IVertexBufferManager* VertexBufferManager,
                        unsigned long* GroupCursor, void* Context, unsigned long StartArg,
                        unsigned long CountArg, unsigned long IndexSpanArg)
    {
        FNomadContext* Submit = (FNomadContext*)(Context);
        if (Submit->Mesh == nullptr || CountArg == 0 || IndexSpanArg == 0 || G_FlmEngineOuter == nullptr)
            return (GENRESULT)(-1);
        FScopedComponentReference WireReference;
        IEngine* Engine = (IEngine*)(G_FlmEngineOuter);
        if (Engine->QueryInstance(Submit->Inst, Iids::VMeshWire, &WireReference.Object) >= 0)
        {
            if (StartArg == 0)
            {
                FScopedWireRenderState Specular(Pipeline, D3DRS_LIGHTING, 1);
                FScopedWireRenderState Color(Pipeline, D3DRS_AMBIENT, WireColor);
                ((IVMeshWire*)WireReference.Object)->Draw();
            }
            return GR_OK;
        }
        FScopedComponentReference MeshReference;
        if (Engine->QueryInstance(Submit->Inst, Iids::Mesh, &MeshReference.Object) < 0)
            return (GENRESULT)(-1);
        IMesh* Mesh = (IMesh*)(MeshReference.Object);
        if (!MeshSelectAndAdvanceCursor(Mesh, GroupCursor))
            return GR_OK;
        return WireDrawEdges(Pipeline, VertexBufferManager, Mesh, WireColor, 0, false);
    }

    // 066fc150..c7d0: only IMesh QI, same status/cursor cleanup, twelve output vertices per edge.
    GENRESULT SubmitWire2Geometry(const unsigned long& ColorFromP0, const unsigned long& ColorFromP1, IRenderPipeline* Pipeline,
                         IVertexBufferManager* VertexBufferManager, unsigned long* GroupCursor, void* Context,
                         unsigned long CountArg, unsigned long IndexSpanArg)
    {
        FNomadContext* Submit = (FNomadContext*)(Context);
        if (Submit->Mesh == nullptr || CountArg == 0 || IndexSpanArg == 0 || G_FlmEngineOuter == nullptr)
            return (GENRESULT)(-1);
        FScopedComponentReference MeshReference;
        IEngine* Engine = (IEngine*)(G_FlmEngineOuter);
        if (Engine->QueryInstance(Submit->Inst, Iids::Mesh, &MeshReference.Object) < 0)
            return (GENRESULT)(-1);
        IMesh* Mesh = (IMesh*)(MeshReference.Object);
        if (!MeshSelectAndAdvanceCursor(Mesh, GroupCursor))
            return GR_OK;
        return WireDrawEdges(Pipeline, VertexBufferManager, Mesh, ColorFromP0, ColorFromP1, true);
    }

    // LAB_066f5dc0: Candidate forwarding or two passes. The resolved handle
    // is borrowed by reference because retail reloads it after draw/query calls.
    GENRESULT SubmitEnvMapGeometry(IRenderPipeline* Pipeline, IRPDraw* RpDraw, IVertexBufferManager* VbMgr,
                              unsigned long EnvMapFlag, const unsigned long& EnvMapResolvedHandle,
                              void* Context, unsigned long Prim, unsigned long Start, unsigned long Count,
                              unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags)
    {
        FNomadContext* Submit = (FNomadContext*)(Context);
        // 066f5dc7: Context is mandatory; Candidate has the actual null branch.
        if (Submit->Candidate == nullptr)
            return (GENRESULT)(-1);
        if (EnvMapFlag == 0)
            return ((IMaterial*)(Submit->Candidate))->Submit(Context, Prim, Start, Count,
                                                                      IbStart, IndexSpan, Flags);

        Pipeline->SetRenderState(D3DRS_CULLMODE, 3);
        const GENRESULT Pass1 = FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start,
                                               Count, IbStart, IndexSpan, Flags);
        Pipeline->SetRenderState(D3DRS_CULLMODE, 2);
        // The retail common tail returns Count even on a failed first pass.
        if (Pass1 < 0)
            return (GENRESULT)(Count);

        if (EnvMapResolvedHandle != 0)
        {
            unsigned long Stage = 0;
            int Probe = 1;
            Pipeline->GetTextureStageState(0, 1, &Probe);
            if (Probe != 1)
            {
                Stage = 1;
                Pipeline->GetTextureStageState(1, 1, &Probe);
            }
            // HRESULT is ignored, and a failed query can retain Probe's seed.
            if (Probe == 1)
            {
                PipelineApplyTssTable(Pipeline, Stage, kEnvMapTss0);
                PipelineApplyTssTable(Pipeline, Stage + 1, kEnvMapTss1);
                Pipeline->SetTextureStageTexture((int)(Stage), (void*)(EnvMapResolvedHandle));
                Pipeline->SetTextureStageState((int)(Stage), D3DTSS_TEXCOORDINDEX, 0x30000);
                Pipeline->SetRenderState(D3DRS_LOCALVIEWER, 1);
                Pipeline->SetRenderState(D3DRS_NORMALIZENORMALS, 1);
            }
        }
        PipelineApplyRsTable(Pipeline, kEnvGlassDrawTailRs);
        FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start, Count, IbStart, IndexSpan,
                        Flags);
        return (GENRESULT)(Count);
    }

    // 066f6b40..6dbc: engine/userdata may be null, Context and material are mandatory.
    GENRESULT SubmitExclusionGeometry(IRenderPipeline* Pipeline, IRPDraw* RpDraw, IVertexBufferManager* VbMgr, void* Context,
                                 unsigned long PrimArg, unsigned long StartArg, unsigned long CountArg,
                                 unsigned long IbStartArg, unsigned long IndexSpanArg, unsigned long FlagsArg,
                                 float* MaterialBlock)
    {
        const unsigned long Prim = PrimArg;
        const unsigned long Start = StartArg;
        const unsigned long Count = CountArg;
        const unsigned long IbStart = IbStartArg;
        const unsigned long IndexSpan = IndexSpanArg;
        const unsigned long Flags = FlagsArg;

        unsigned long Old8B = 0;
        PipelineGetRenderState(Pipeline, D3DRS_AMBIENT, &Old8B);
        PipelineSetRenderState(Pipeline, D3DRS_AMBIENT, (unsigned long)(-1));

        if (G_FlmEngineOuter != nullptr)
        {
            FNomadContext* Submit = (FNomadContext*)(Context);
            void* Result = ((IEngine*)(G_FlmEngineOuter))->GetInstanceUserData(Submit->Inst);
            if (Result != nullptr)
            {
                const unsigned char* Bytes = (const unsigned char*)(Result);
                std::memcpy(&MaterialBlock[3], Bytes + 0x10, sizeof(float));
                std::memcpy(&MaterialBlock[4], Bytes, 3 * sizeof(float));
            }
        }

        SetMaterialBlock(Pipeline, MaterialBlock);

        FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start, Count, IbStart, IndexSpan,
                        Flags);

        PipelineSetRenderState(Pipeline, D3DRS_AMBIENT, Old8B);
        return GR_OK;
    }

    // Glass-family draw path shares the same argument contract.
    GENRESULT SubmitGlassGeometry(IRenderPipeline* Pipeline, IRPDraw* RpDraw, IVertexBufferManager* VbMgr, void* Context,
                             unsigned long PrimArg, unsigned long StartArg, unsigned long CountArg, unsigned long IbStartArg,
                             unsigned long IndexSpanArg, unsigned long FlagsArg)
    {
        const unsigned long Prim = PrimArg;
        const unsigned long Start = StartArg;
        const unsigned long Count = CountArg;
        const unsigned long IbStart = IbStartArg;
        const unsigned long IndexSpan = IndexSpanArg;
        const unsigned long Flags = FlagsArg;

        PipelineSetRenderState(Pipeline, D3DRS_CULLMODE, 3);
        const GENRESULT Pass1 = FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start,
                                                Count, IbStart, IndexSpan, Flags);
        PipelineSetRenderState(Pipeline, D3DRS_CULLMODE, 2);
        if (Pass1 < 0)
            return Pass1;

        PipelineApplyRsTable(Pipeline, kEnvGlassDrawTailRs);

        return FlmMeshDrawPass(Pipeline, RpDraw, VbMgr, Context, Prim, Start, Count, IbStart,
                              IndexSpan, Flags);
    }
}

FMaterialImpl::FMaterialImpl(const FMaterialSpec& Spec)
    : Inner_(this),
      ControllingOuter_(&Inner_),
      Kind_(Spec.Kind),
      FactoryName_(Spec.Name),
      Outer_(nullptr),
      Pipeline_(nullptr),
      TextureLibrary_(nullptr),
      VertexBufferManager_(nullptr),
      RpDraw_(nullptr),
      MaterialLibrary_(nullptr),
      TextureBlock_(nullptr),
      Flag64_(0),
      Flag65_(0),
      bBackdrop_(0),
      bBackdropNameMatchCopy_(0),
      bNomadRegular_(0),
      bSecondTexture_(0),
      Pad71_(0),
      NomadDead68_(0.0f),
      NomadLive6C_(0.0f),
      HudAnimFlag_(0),
      MatAnim_(nullptr),
      EnvMapFlag_(0),
      EnvMapResolvedHandle_(0),
      ShadowHandle_(0),
      ShadowTextureLibrary_(nullptr),
      ShadowTextureRecord_(nullptr),
      ShadowResolvedHandle_(0),
      ShadowLayerCount_(0),
      bShadowTextureInitialized_(false),
      EnvMapTexNameCrc_(0),
      StageCounter_(0),
      WireColorR_(0.0f),
      WireColorG_(0.0f),
      WireColorB_(0.0f),
      WireField74_(0.0f),
      WireField78_(0),
      WireField7C_(0),
      WireGroupIndex_(0),
      Exclusion68_(0),
      ColorizeExtraW_(0.0f),
      ColorizeOptionsFlags_(0),
      bInstanceNameSet_(false),
      TextureVerifyCount_(0)
{
    for (int Index = 0; Index < 3; ++Index)
        NomadDiffuseColor_[Index] = 0.0f;
    for (int Index = 0; Index < 17; ++Index)
        MaterialBlock_[Index] = 0.0f;
    MaterialBlock_[0] = 1.0f;
    MaterialBlock_[5] = 1.0f;
    MaterialBlock_[10] = 1.0f;
    // Colorize 066f45bd..066f45d8, Wire 066fafc1..066fafdc and
    // Wire2 066fbc51..066fbc6c clear all 17 words, then seed only
    // Diffuse and Ambient RGBA to white.
    if (Kind_ == KindColorize || Kind_ == KindWire || Kind_ == KindWire2 || Kind_ == KindShadow)
    {
        for (int Index = 0; Index < 17; ++Index)
            MaterialBlock_[Index] = Index < 8 ? 1.0f : 0.0f;
    }
    // EngineGlow factory 066f529b..066f52c8 seeds diffuse/ambient white,
    // while every later material-block word remains zero (not an identity matrix).
    if (Kind_ == KindEngineGlow || Kind_ == KindHudAnim)
    {
        // HUDAnim factory 066ffa98..066ffabc has the same white block.
        for (int Index = 0; Index < 17; ++Index)
            MaterialBlock_[Index] = Index < 8 ? 1.0f : 0.0f;
    }
    InstanceName_ = nullptr;
}

FMaterialImpl::~FMaterialImpl() noexcept(false)
{
    if (Kind_ == KindShadow)
    {
        // 066faad0 / unwind06700d30: base cleanup is owed even if ReleaseTextureRecord throws.
        struct FShadowBaseCleanup
        {
            FMaterialImpl* Owner;
            void (FMaterialImpl::*Cleanup)();
            ~FShadowBaseCleanup() noexcept(false) { (Owner->*Cleanup)(); }
        } BaseCleanup{this, &FMaterialImpl::ReleaseMaterialResources};
        if (ShadowTextureRecord_ != nullptr)
        {
            ShadowTextureLibrary_->ReleaseTextureRecord(ShadowTextureRecord_);
            ShadowResolvedHandle_ = 0;
            ShadowTextureRecord_ = nullptr;
        }
        return;
    }

    if (Kind_ == KindPlanetWater)
    {
        // 066fa160: the base lifetime covers held animation Release.
        struct FPlanetWaterBaseCleanup
        {
            FMaterialImpl* Owner;
            void (FMaterialImpl::*Cleanup)();
            ~FPlanetWaterBaseCleanup() noexcept(false) { (Owner->*Cleanup)(); }
        } BaseCleanup{this, &FMaterialImpl::ReleaseMaterialResources};
        if (MatAnim_ != nullptr)
        {
            MatAnim_->Release();
            MatAnim_ = nullptr;
        }
        return;
    }

    // EnvMap FUN_066f6450: release its separately retained texture before base teardown.
    if (Kind_ == KindEnvMap)
    {
        // 06700c30 -> 066f6ec0: the base survives an exception from ReleaseTextureRecord.
        struct FEnvMapBaseCleanup
        {
            FMaterialImpl* Owner;
            void (FMaterialImpl::*Cleanup)();
            ~FEnvMapBaseCleanup() noexcept(false) { (Owner->*Cleanup)(); }
        } BaseCleanup{this, &FMaterialImpl::ReleaseMaterialResources};
        if (TextureLibrary_ != nullptr && ShadowHandle_ != 0)
        {
            TextureLibrary_->ReleaseTextureRecord(
                (void*)(ShadowHandle_));
        }
        ShadowHandle_ = 0;
        EnvMapTexNameCrc_ = 0;
        return;
    }
    if (Kind_ == KindHudAnim)
    {
        // 06700eb0 -> 066f6ec0: the base survives an exception from animation Release.
        struct FHudAnimBaseCleanup
        {
            FMaterialImpl* Owner;
            void (FMaterialImpl::*Cleanup)();
            ~FHudAnimBaseCleanup() noexcept(false) { (Owner->*Cleanup)(); }
        } BaseCleanup{this, &FMaterialImpl::ReleaseMaterialResources};
        if (MatAnim_ != nullptr)
        {
            MatAnim_->Release();
            MatAnim_ = nullptr;
        }
        return;
    }
    // Remaining material kinds have no derived animation cleanup in retail.
    if (MatAnim_ != nullptr)
    {
        MatAnim_->Release();
        MatAnim_ = nullptr;
    }
    ReleaseMaterialResources();
}

void FMaterialImpl::ReleaseMaterialResources()
{
    for (unsigned long Index = 0; Index < TextureVerifyCount_; ++Index)
    {
        unsigned char* Entry = (unsigned char*)(TextureBlock_) +
                               Index * kFlmTextureVerifyEntrySize;
        int* PlaybackGroupId = (int*)(Entry + 4);
        if (*PlaybackGroupId != -1)
        {
            TextureLibrary_->DecRefById(*PlaybackGroupId);
            *PlaybackGroupId = -1;
        }
    }
    // 06700643 -> 0670071b: all groups are released before reverse element teardown.
    if (TextureBlock_ != nullptr)
    {
        for (unsigned long Remaining = TextureVerifyCount_; Remaining != 0; --Remaining)
        {
            unsigned char* Entry = (unsigned char*)(TextureBlock_) +
                                   (Remaining - 1) * kFlmTextureVerifyEntrySize;
            std::free(*(void**)(Entry));
            *(void**)(Entry) = nullptr;
        }
    }
    delete[] TextureBlock_;
    TextureBlock_ = nullptr;
    TextureVerifyCount_ = 0;
    TextureLibrary_ = nullptr;
    Pipeline_ = nullptr;
    VertexBufferManager_ = nullptr;
    std::free(InstanceName_); // 06700661: owned name, after texture-array destruction.
    InstanceName_ = nullptr;
    // MaterialLibrary_ is borrowed from the outer container, just like Pipeline_.
}

void FMaterialImpl::ConfigureAggregation(FMaterialAggregateDesc* Desc)
{
    // 066f52cb..066f52fc: controller is installed before Slot22.
    ControllingOuter_ = Desc->OuterAt8;
    if (ControllingOuter_ == nullptr)
        ControllingOuter_ = &Inner_;
    else
        *Desc->InnerOutAtC = &Inner_;
}

GENRESULT DACOM_API FMaterialImpl::QueryInterface(const char* Iid, void** Out)
{
    // LAB_066f51c0: the public face always delegates to its controlling outer.
    return ControllingOuter_->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMaterialImpl::AddRef()
{
    // LAB_066f58f0: no local counter operation on the public face.
    return ControllingOuter_->AddRef();
}

unsigned long DACOM_API FMaterialImpl::Release()
{
    // LAB_066f5900: the controller owns destruction of this material.
    return ControllingOuter_->Release();
}

GENRESULT DACOM_API FMaterialImpl::FInner::QueryInterface(const char* Iid, void** Out)
{
    // LAB_066f5830, map 06701980: one IMaterial face at offset zero, then null.
    if (std::strcmp(Iid, Iids::Material) == 0)
    {
        IMaterial* Face = Owner;
        Face->AddRef(); // AddRef precedes publishing the output, and may call the outer.
        *Out = Face;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FMaterialImpl::FInner::AddRef()
{
    return ++RefCount;
}

unsigned long DACOM_API FMaterialImpl::FInner::Release()
{
    // LAB_066f5190: saturating decrement, reset before callback into owner teardown.
    if (RefCount != 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        if (Owner != nullptr)
            Owner->ScalarDeletingDestructor(1);
    }
    return Remaining;
}

GENRESULT DACOM_API FMaterialImpl::InnerQueryInterface(const char* Iid, void** Out)
{
    return Inner_.QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMaterialImpl::InnerAddRef()
{
    return Inner_.AddRef();
}

unsigned long DACOM_API FMaterialImpl::InnerRelease()
{
    return Inner_.Release();
}

GENRESULT DACOM_API FMaterialImpl::Initialize(IDAComponent* Outer)
{
    // FUN_066F1210 — QI then Release (container holds the objects).
    if (Outer == nullptr)
        return GR_GENERIC;

    Outer_ = Outer;
    GENRESULT Result = Outer->QueryInterface(Iids::TextureLibraryA, (void**)(&TextureLibrary_));
    if (Result < 0)
    {
        FlmFdump(kFdumpTrace1, kImplFile, 0xAD, "TRACE_1",
                 "IMaterialImpl: intitialize: this material requires IID_ITextureLibrary\n");
        return GR_GENERIC;
    }
    TextureLibrary_->Release();

    Result = Outer->QueryInterface(Iids::RenderPipeline8B, (void**)(&Pipeline_));
    if (Result < 0)
    {
        FlmFdump(kFdumpTrace1, kImplFile, 0xB3, "TRACE_1",
                 "IMaterialImpl: intitialize: this material requires IID_IRenderPipeline\n");
        return GR_GENERIC;
    }
    Pipeline_->Release();

    Result = Outer->QueryInterface(Iids::RPDraw, (void**)(&RpDraw_));
    if (Result < 0)
    {
        FlmFdump(kFdumpTrace1, kImplFile, 0xB9, "TRACE_1",
                 "IMaterialImpl: intitialize: this material requires IID_IRPDraw\n");
        return GR_GENERIC;
    }
    RpDraw_->Release();

    Result = Outer->QueryInterface(Iids::VertexBufferManager,
                                   (void**)(&VertexBufferManager_));
    if (Result < 0)
    {
        FlmFdump(kFdumpTrace1, kImplFile, 0xBF, "TRACE_1",
                 "IMaterialImpl: intitialize: this material requires IID_IVertexBufferManager\n");
        return GR_GENERIC;
    }
    VertexBufferManager_->Release();

    void* ParamBase = nullptr;
    const unsigned long ParamCount = GetParameterTable(&ParamBase);
    unsigned char* Descriptors = (unsigned char*)(ParamBase);
    TextureVerifyCount_ = 0;
    for (unsigned long Index = 0; Index < ParamCount; ++Index)
    {
        const unsigned char* Entry = Descriptors + Index * kFlmParamDescSize;
        if (*(const unsigned long*)(Entry + 0x10) == 7)
            ++TextureVerifyCount_;
    }
    if (TextureVerifyCount_ != 0)
    {
        const size_t WordCount = (size_t)(TextureVerifyCount_) *
                                 (kFlmTextureVerifyEntrySize / sizeof(unsigned long));
        TextureBlock_ = new unsigned long[WordCount];
        for (unsigned long Index = 0; Index < TextureVerifyCount_; ++Index)
        {
            unsigned char* Entry = (unsigned char*)(TextureBlock_) +
                                   Index * kFlmTextureVerifyEntrySize;
            *(void**)(Entry) = nullptr;
            *(int*)(Entry + 4) = -1;
            *(unsigned long*)(Entry + 8) = 0;
            *(unsigned long*)(Entry + 0x1C) = 4;
            *(unsigned long*)(Entry + 0x20) = 0;
            *(unsigned long*)(Entry + 0x24) = 0;
            Entry[0x28] = 0;
        }
    }
    for (unsigned long Index = 0; Index < ParamCount; ++Index)
    {
        unsigned char* Entry = Descriptors +
                               Index * kFlmParamDescSize;
        StoreParameterValue(Index, Entry, Entry + 0x10);
    }

    if (Kind_ == KindHudAnim)
    {
        // LAB_066ffee0: common success, then QI/Release; its HRESULT is ignored.
        Outer->QueryInterface(Iids::MaterialLibrary, (void**)(&MaterialLibrary_));
        MaterialLibrary_->Release(); // 066fff06..0b: outer container owns the object.
    }

    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::LoadFromFileSystem(void* FileSystem)
{
    // LAB_066f13e0: bit0 skips loading; failed mandatory (bit2) entries
    // report a diagnostic, while the loop and final success continue.
    void* Base = nullptr;
    const unsigned long Count = GetParameterTable(&Base);
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        unsigned char* Entry = (unsigned char*)(Base) + Index * kFlmParamDescSize;
        if ((*Entry & 1) != 0)
            continue;
        if (!FlmLoadDeclaredParameter(this, FileSystem, Entry, Index) &&
            (*(const unsigned long*)(Entry) & 4) != 0)
        {
            const char* Instance = bInstanceNameSet_ ? InstanceName_ : "(Unnamed)";
            const char* Class = ClassName();
            const char* Description = *(const char* const*)(Entry + 8);
            char Message[0x2000];
            FlmFormatMessage(Message, "Unable to load parameter '%s' for '%s' (%s)\n",
                      Description, Instance, Class);
            FDUMP(kFdumpTrace1, "%s(%d) : TRACE_1:General:%s", kImplFile, 0xfd, Message);
        }
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::SaveProperties(void* FileSystem)
{
    // LAB_066f14f0: save each descriptor with bit 0 clear. A failed write is
    // reported, but the loop continues and returns -1 after the last entry.
    void* Base = nullptr;
    const unsigned long Count = GetParameterTable(&Base);

    GENRESULT Result = GR_OK;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        unsigned char* Entry = (unsigned char*)(Base) + Index * kFlmParamDescSize;
        if ((*Entry & 1) != 0)
            continue;
        if (!FlmSaveParameterToFileSystem(this, FileSystem, Entry, Index))
        {
            const char* Instance = bInstanceNameSet_ ? InstanceName_ : "(Unnamed)";
            const char* Class = ClassName();
            const char* Description = *(const char* const*)(Entry + 8);
            char Message[0x2000];
            FlmFormatMessage(Message, "Unable to save parameter '%s' for '%s' (%s)\n",
                      Description, Instance, Class);
            FDUMP(kFdumpTrace1, "%s(%d) : TRACE_1:General:%s", kImplFile, 0x119, Message);
            Result = (GENRESULT)(-1);
        }
    }
    return Result;
}

GENRESULT DACOM_API FMaterialImpl::ApplyVerify(void* Context, void* Extra)
{
    if (Kind_ == KindCloak || Kind_ == KindColorize)
        return GR_OK;

    if (Kind_ == KindWire2)
    {
        ApplyInitialRenderStates(nullptr);
        PipelineVerifyState(Pipeline_);
        const GENRESULT Verify = PipelineVerifyState(Pipeline_);
        if (Verify != GR_OK)
        {
            char Message[0x2000];
            FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kWire2File, 0x78,
                FlmFormatMessage(Message, "failed to verify material %s", InstanceName_));
        }
        return Verify;
    }

    if (Kind_ == KindPlanetWater)
    {
        struct FAnimationLibraryReference
        {
            IMatAnimLibrary* Object = nullptr;
            ~FAnimationLibraryReference() noexcept(false)
            {
                if (Object != nullptr)
                {
                    Object->Release();
                    Object = nullptr;
                }
            }
        } AnimLib;
        if (Pipeline_->QueryInterface(Iids::MatAnimLibrary, (void**)&AnimLib.Object) >= 0)
        {
            struct FAnimationNameQuery { const char* Name; unsigned long Crc; };
            FAnimationNameQuery Query = {InstanceName_, DACOM_CRC::GetCRC32(InstanceName_)};
            if (MatAnim_ != nullptr)
            {
                MatAnim_->Release();
                MatAnim_ = nullptr;
            }
            AnimLib.Object->CreateAnimationInstance(-1, &Query, &MatAnim_);
        }
        Setup();
        Bind(nullptr);
        const GENRESULT Verify = PipelineVerifyState(Pipeline_);
        Teardown(nullptr);
        return Verify;
    }

    if (Kind_ == KindNomad)
    {
        // 066f8c02: enable the second texture before the first bind.
        Setup();
        bSecondTexture_ = 1;
        GENRESULT BindResult = Bind(nullptr);
        GENRESULT VerifyResult = BindResult;
        bool bFailed = (long)(BindResult) < 0;
        if (!bFailed)
        {
            VerifyResult = PipelineVerifyState(Pipeline_);
            bFailed = (long)(VerifyResult) < 0;
        }
        bSecondTexture_ = bFailed ? 0 : 1;
        if (bFailed)
        {
            Bind(nullptr);
            VerifyResult = PipelineVerifyState(Pipeline_);
        }
        Teardown(nullptr);
        return VerifyResult;
    }

    if (Kind_ == KindEngineGlow)
    {
        // Reset the material block before EnvMap setup.
        for (int Index = 0; Index < 17; ++Index)
            MaterialBlock_[Index] = 0.0f;
        MaterialBlock_[3] = 1.0f;
        MaterialBlock_[12] = 0.75f;
        MaterialBlock_[13] = 0.7f;
        MaterialBlock_[14] = 0.7f;
        MaterialBlock_[15] = 1.0f;
    }

    Setup();
    Bind(nullptr);
    const GENRESULT Verify = PipelineVerifyState(Pipeline_);
    Teardown(nullptr);
    return Verify;
}

GENRESULT DACOM_API FMaterialImpl::ResolveTextures(float Delta)
{
    if (Kind_ == KindNomad)
    {
        if (bNomadRegular_ != 0)
            NomadDead68_ = (float)((double)(Delta) + NomadDead68_);
        NomadLive6C_ = (float)((double)(Delta) + NomadLive6C_);
    }

    if (Kind_ == KindHudAnim)
    {
        // LAB_066fff20 resolves texture records before its HUD animation tail.
        for (unsigned long Index = 0; Index < TextureVerifyCount_; ++Index)
        {
            unsigned char* Entry = (unsigned char*)(TextureBlock_) +
                                   Index * kFlmTextureVerifyEntrySize;
            const int PlaybackGroupId = *(const int*)(Entry + 4);
            unsigned long* Handle = (unsigned long*)(Entry + 8);
            if (PlaybackGroupId == -1)
                *Handle = 0;
            else if (Entry[0x28] == 0)
                TextureLibrary_->GetFrame(PlaybackGroupId, -1, Handle);
        }

        if (MaterialLibrary_ != nullptr)
        {
            unsigned long OutValue = 0;
            const GENRESULT Got =
                MaterialLibrary_->GetGlobalRecordValue(4, &OutValue);
            if (Got >= 0)
                HudAnimFlag_ = (OutValue >= 1) ? 1 : 0;
        }

        // FUN_066FFF80 0x066fffc0..0x0670006f: QI 1.11_IMatAnimLibrary on this+4 (the pipeline,
        // no null test); a failed QI (JL) goes straight to the release of the out pointer.
        FScopedComponentReference AnimLib;
        if (Pipeline_->QueryInterface(Iids::MatAnimLibrary, &AnimLib.Object) >= 0)
        {
            if (HudAnimFlag_ != 0)
            {
                // 0x066fffed..0x0670002f: only while +0x68 is still empty -- {name +0x14, its
                // CRC}, the (dead) release of +0x68, then the library's slot 5(-1, &rec, +0x68).
                if (MatAnim_ == nullptr)
                {
                    const char* Name = InstanceName_;
                    unsigned long CrcStruct[2];
                    CrcStruct[0] = (unsigned long)(Name);
                    CrcStruct[1] = DACOM_CRC::GetCRC32(Name);
                    if (MatAnim_ != nullptr)
                    {
                        MatAnim_->Release();
                        MatAnim_ = nullptr;
                    }
                    ((IMatAnimLibrary*)(AnimLib.Object))->CreateAnimationInstance(-1, CrcStruct, &MatAnim_);
                }
            }
            else if (MatAnim_ != nullptr)
            {
                // 0x06700031..0x06700042: the flag clear -- +0x68 released and zeroed.
                MatAnim_->Release();
                MatAnim_ = nullptr;
            }
        }
        // Held library is released on normal and EH exit (06700e90).
        return GR_OK;
    }

    // LAB_066f1610: mandatory storage/library, unsigned count re-read each iteration.
    for (unsigned long Index = 0; Index < TextureVerifyCount_; ++Index)
    {
        unsigned char* Entry = (unsigned char*)(TextureBlock_) +
                               Index * kFlmTextureVerifyEntrySize;
        const int PlaybackGroupId = *(const int*)(Entry + 4);
        unsigned long* Handle = (unsigned long*)(Entry + 8);
        if (PlaybackGroupId == -1)
            *Handle = 0;
        else if (Entry[0x28] == 0)
            TextureLibrary_->GetFrame(PlaybackGroupId, -1, Handle);
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::Bind(void* Context)
{
    switch (Kind_)
    {
    case KindGlass:
        BindGlass(Context);
        break;
    case KindNomad:
        BindNomad();
        break;
    case KindBackdrop:
        BindBackdrop(Context);
        break;
    case KindHudIcon:
        BindHudIcon(Context);
        break;
    case KindHudAnim:
        BindHudAnim(Context);
        break;
    case KindColorize:
        return BindColorize(Context);
    case KindCloak:
        BindCloak(Context);
        break;
    case KindWire:
        BindWire();
        break;
    case KindWire2:
        BindWire2(Context);
        break;
    case KindExclusion:
        BindExclusion();
        break;
    case KindEnvMap:
        BindEnvMap(Context);
        break;
    case KindShadow:
        return BindShadow(Context);
    case KindEngineGlow:
        BindEngineGlow(Context);
        break;
    case KindVisibleCounter:
        BindVisibleCounter();
        break;
    case KindPlanetWater:
        BindPlanetWater(Context);
        break;
    default:
        BindUnknown(FactoryName_);
        break;
    }
    if (Kind_ == KindBackdrop)
        return (GENRESULT)(-1);
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::Teardown(void* Context)
{
    if (Kind_ == KindBackdrop)
        return TeardownBackdrop(Context);

    if (Kind_ == KindShadow)
        return ShadowCandidateDispatch(Context);

    if (Kind_ == KindCloak || Kind_ == KindColorize)
    {
        // LAB_066f42e0 (Cloak) / LAB_066f5010 (Colorize) -- see TeardownCandidateIgnoringResult.
        TeardownCandidateIgnoringResult(Context);
        return GR_OK;
    }

    if (Kind_ == KindEnvMap)
    {
        // EnvMap restores its dynamic stage state using the retained texture handle.
        if (ShadowHandle_ != 0)
        {
            PipelineSetRenderState(Pipeline_, D3DRS_NORMALIZENORMALS, 0);
            PipelineSetRenderState(Pipeline_, D3DRS_LOCALVIEWER, 0);
            PipelineSetTextureStageState(Pipeline_, 0, D3DTSS_TEXCOORDINDEX, 0);
            PipelineSetTextureStageState(Pipeline_, 1, D3DTSS_TEXCOORDINDEX, 1);
        }
        TeardownCandidateIgnoringResult(Context);
        return GR_OK;
    }

    // The material's pipeline (+0x4) is never null-tested in flmaterials.dll: it is set by
    // Initialize's QI, and a failed QI fails the material's creation.
    if (Kind_ == KindNomad)
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 0);

    if (Kind_ == KindPlanetWater && MatAnim_ != nullptr)
        PipelineSetTextureStageState(Pipeline_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);

    if (Kind_ == KindHudAnim && HudAnimFlag_ != 0)
    {
        if (MatAnim_ != nullptr)
            PipelineSetTextureStageState(Pipeline_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
        if (ContextWantsColorVertex(Context))
            PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 0);
    }

    // Reset per-vertex color only for materials and contexts that enabled it.
    if ((Kind_ == KindEngineGlow || Kind_ == KindExclusion || Kind_ == KindGlass ||
         Kind_ == KindHudIcon || Kind_ == KindWire || Kind_ == KindWire2 ||
         Kind_ == KindPlanetWater) &&
        ContextWantsColorVertex(Context))
    {
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 0);
    }

    if (Kind_ == KindVisibleCounter)
        return GR_OK;

    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::Submit(void* Context, unsigned long PrimType, unsigned long Start, unsigned long Count, unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags)
{
    if (Kind_ == KindVisibleCounter)
    {
        // LAB_066FADC0: B4++, B8 += [ESP+0x1C] (IndexSpan), BC += [ESP+0x14] (Count).
        G_VisibleCounterA += 1;
        G_VisibleCounterB += IndexSpan;
        G_VisibleCounterC += Count;
        return GR_OK;
    }

    if (Kind_ == KindShadow)
        return DrawShadow(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);

    if (Kind_ == KindNomad)
    {
        return SubmitNomadGeometry(bSecondTexture_, NomadLive6C_, StageCounter_, VertexBufferManager_,
                           Pipeline_, Context, PrimType, Start, Count, IbStart, IndexSpan);
    }

    if (Kind_ == KindHudAnim)
    {
        // LAB_06700260.
        return SubmitHudAnimatedGeometry(HudAnimFlag_, RpDraw_, Pipeline_, VertexBufferManager_, Context, PrimType, Start, Count,
                             IbStart, IndexSpan, Flags);
    }

    if (Kind_ == KindPlanetWater)
    {
        // FUN_066f9ea0.
        return SubmitPlanetWaterGeometry(Pipeline_, RpDraw_, VertexBufferManager_, Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    }

    if (Kind_ == KindWire)
    {
        return SubmitWireGeometry(WireField78_, Pipeline_, VertexBufferManager_, &WireGroupIndex_, Context, Start,
                          Count, IndexSpan);
    }

    if (Kind_ == KindWire2)
    {
        // 066fc150: IMesh path; Color0 for original points, Color1 for shifted duplicates.
        // Four triangles (twelve vertices) per edge, including shifted P0 with Color1.
        return SubmitWire2Geometry(WireField78_, WireField7C_, Pipeline_, VertexBufferManager_,
                           &WireGroupIndex_, Context, Count, IndexSpan);
    }

    if (Kind_ == KindBackdrop)
        return DrawBackdrop(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);

    if (Kind_ == KindCloak)
    {
        // 066f4258 reads the material's +64 flag, not the context.
        if (Context == nullptr || ((FNomadContext*)(Context))->Candidate == nullptr)
            return GR_OK;
        unsigned long ForwardFlags = Flags;
        if (Flag64_ != 0)
            ForwardFlags |= 0x400;
        return SubmitCandidateGeometry(Context, PrimType, Start, Count, IbStart, IndexSpan,
                                      ForwardFlags);
    }

    if (Kind_ == KindEngineGlow || Kind_ == KindColorize || Kind_ == KindHudIcon)
    {
        // LAB_066f3370 -- shared Slot10 draw
        // override for these 3 kinds, XREF-confirmed against each instance vtable's +0x28.
        return SubmitRpDrawGeometry(RpDraw_, Pipeline_, VertexBufferManager_, Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    }

    if (Kind_ == KindEnvMap)
    {
        return SubmitEnvMapGeometry(Pipeline_, RpDraw_, VertexBufferManager_, EnvMapFlag_,
                               EnvMapResolvedHandle_, Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    }

    if (Kind_ == KindExclusion)
    {
        return SubmitExclusionGeometry(Pipeline_, RpDraw_, VertexBufferManager_, Context, PrimType, Start, Count, IbStart, IndexSpan, Flags,
                                  MaterialBlock_);
    }

    if (Kind_ == KindGlass)
    {
        return SubmitGlassGeometry(Pipeline_, RpDraw_, VertexBufferManager_, Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    }

    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::SetName(const char* Name)
{
    // LAB_066f1670..16d9: free old, null, allocate min(strlen+1, 64), lowercase.
    std::free(InstanceName_);
    InstanceName_ = nullptr;
    bInstanceNameSet_ = false;
    if (Name != nullptr)
    {
        size_t NameBytes = std::strlen(Name) + 1;
        if (NameBytes > 64)
            NameBytes = 64;
        InstanceName_ = (char*)(std::malloc(NameBytes));
        if (InstanceName_ == nullptr)
            throw std::bad_alloc(); // Same allocation failure as retail operator new.
        std::strncpy(InstanceName_, Name, NameBytes);
        InstanceName_[NameBytes - 1] = '\0';
        _strlwr(InstanceName_);
        bInstanceNameSet_ = true;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::GetName(char* Out, unsigned long Size)
{
    // LAB_066f16e0..1710: the only early return is an unset name.
    if (InstanceName_ == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, InstanceName_, Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::CopyClassName(char* Out, unsigned long Size)
{
    // FUN_066f1720: two virtual class-name reads; mandatory output and size.
    if (ClassName() == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, ClassName(), Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::GetPassCount(int* Out)
{
    if (Out != nullptr)
        *Out = 1;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::GetRenderFlags(unsigned long* Out)
{
    unsigned long Mask = 8;
    if (Kind_ == KindColorize)
    {
        // 066f4a90: TEST AH,05 / JP skips the extra bit on >= or unordered;
        // fall-through is strictly less than the retail double 1.0 constant.
        if (Flag64_ != 0 || (double)(ColorizeExtraW_) < 1.0)
            Mask |= 2;
    }
    else if (Kind_ == KindCloak)
        Mask = 0x2a; // 066f3ff0
    else if (Kind_ == KindWire || Kind_ == KindWire2)
        Mask = 0xa0; // 066fb1e0
    else if (Kind_ == KindNomad)
        Mask = 0xa2; // 066f8930
    else if (Kind_ == KindExclusion || Kind_ == KindHudIcon)
        Mask = Flag64_ != 0 ? 0x2a : 0x28; // 066f8010
    else if (Kind_ == KindEngineGlow || Kind_ == KindEnvMap ||
             Kind_ == KindVisibleCounter || Kind_ == KindHudAnim)
        Mask = Flag64_ != 0 ? 0x0a : 0x08; // 066f56e0
    // 066f2d80: the XOR ECX,ECX discards the preceding Flag64 mask;
    // Backdrop/Starsphere, Glass, PlanetWater and Shadow always answer 8.
    *Out = Mask;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::GetParameterCount(unsigned long* OutCount)
{
    // 066f1770: virtual Slot27(null), then mandatory output publication.
    *OutCount = GetParameterTable(nullptr);
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::GetParameterDescriptor(unsigned long Index, void* OutDesc)
{
    // Copy the indexed declared-property descriptor including its owned typed values.
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Count <= Index)
        return (GENRESULT)-1;

    const FShadingPropertyRecord* Entry = (const FShadingPropertyRecord*)Base + Index;
    FShadingPropertyRecord* Out = (FShadingPropertyRecord*)OutDesc;
    // Preserve the four ordered stores even when the output aliases the source.
    Out->Flags = Entry->Flags;
    Out->Name = Entry->Name;
    Out->Description = Entry->Description;
    Out->Callback = Entry->Callback;

    FlmAssignTypedValue(&Out->Current, &Entry->Current);
    FlmAssignTypedValue(&Out->Default, &Entry->Default);
    FlmAssignTypedValue(&Out->Saved, &Entry->Saved);
    return GR_OK;
}

GENRESULT DACOM_API FMaterialImpl::GetParameterValue(unsigned long Index, void* InOutDesc)
{
    // Read the live property into the caller's typed value with ownership preserved.
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Count <= Index)
        return (GENRESULT)-1;

    const FShadingPropertyRecord* Entry = (const FShadingPropertyRecord*)Base + Index;
    const unsigned long RealType = Entry->Current.Type;

    FShadingPropertyValue* Result = (FShadingPropertyValue*)InOutDesc;
    if (Result->Type != RealType)
        FlmReleaseTypedValue((unsigned long*)Result);
    Result->Type = RealType;

    void* Raw = GetParameterAddress(Index);
    if (Raw == nullptr)
        return (GENRESULT)-1;
    const unsigned long* RawWords = (const unsigned long*)Raw;

    // 066f1c29 reloads the output type after virtual Slot28 returns.
    switch (Result->Type)
    {
    case 3:
        Result->Payload2 = RawWords[2];
        Result->Payload1 = RawWords[1];
        Result->Payload0 = RawWords[0];
        return GR_OK;
    case 4:
        Result->Payload3 = RawWords[3];
        Result->Payload2 = RawWords[2];
        Result->Payload1 = RawWords[1];
        Result->Payload0 = RawWords[0];
        return GR_OK;
    case 1:
    case 2:
    case 8:
    case 9:
    case 10:
        Result->Payload0 = RawWords[0];
        return GR_OK;
    case 5:
    case 7:
        FlmAssignString((unsigned long*)Result, (const char*)RawWords[0]);
        return GR_OK;
    case 6:
        FlmAssignInterface((unsigned long*)Result, (IDAComponent*)RawWords[0]);
        return GR_OK;
    default:
        return GR_OK; // 066f1c2f -> 066f1d02 for unrecognized types.
    }
}

GENRESULT DACOM_API FMaterialImpl::SetParameterValue(unsigned long Index, void* Value)
{
    // Validate before storing the indexed property.
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Count <= Index)
        return (GENRESULT)-1;

    FShadingPropertyRecord* Entry = (FShadingPropertyRecord*)Base + Index;
    if (!ValueWithinDeclaredRange(Index, Entry, Value))
        return (GENRESULT)-1;
    StoreParameterValue(Index, Entry, Value);
    return GR_OK; // 066f1d8f: XOR EAX,EAX, independent of Slot26 result.
}

const char* __thiscall FMaterialImpl::ClassName()
{
    if (Kind_ == KindGlass)
        return G_NameGlassMaterial;
    if (Kind_ == KindNomad)
        return G_NameNomadMaterial;
    if (Kind_ == KindPlanetWater)
        return G_NamePlanetWaterMaterial;
    if (Kind_ == KindShadow)
        return G_NameShadowMaterial;
    if (Kind_ == KindVisibleCounter)
        return G_NameVisibleCounterMaterial;
    if (Kind_ == KindBackdrop)
        return (bBackdrop_ != 0) ? G_NameBackdropMaterial : G_NameStarsphereMaterial;
    return FactoryName_;
}

void* __thiscall FMaterialImpl::ScalarDeletingDestructor(unsigned char bDelete)
{
    // Destruction runs once; bit 0 selects deallocation. Return the original address.
    void* Storage = this;
    this->~FMaterialImpl();
    if ((bDelete & 1) != 0)
        ::operator delete(Storage);
    return Storage;
}

GENRESULT __thiscall FMaterialImpl::InitializeFromDescriptor(void* Arg)
{
    if (Kind_ == KindNomad)
    {
        // 066f884b/884e: both accumulators reset before the required name lookup.
        NomadLive6C_ = 0.0f;
        NomadDead68_ = 0.0f;
        const FMaterialAggregateDesc* Agg = (const FMaterialAggregateDesc*)(Arg);
        bNomadRegular_ = std::strcmp(Agg->NameAt4, G_NameNomadNoBendy) == 0 ? 0 : 1;
        bSecondTexture_ = 1;
        return GR_OK;
    }

    if (Kind_ == KindBackdrop)
    {
        const FMaterialAggregateDesc* Agg = (const FMaterialAggregateDesc*)(Arg);
        bBackdrop_ = std::strcmp(Agg->NameAt4, G_NameBackdropMaterial) == 0 ? 1 : 0;
        bBackdropNameMatchCopy_ = bBackdrop_; // 066f2d2f/2d47: identical stored name-equality predicate.
        return GR_OK;
    }

    if (Kind_ == KindWire || Kind_ == KindWire2)
    {
        // Reset the Wire-family color to white.
        WireColorR_ = 1.0f;
        WireColorG_ = 1.0f;
        WireColorB_ = 1.0f;
        WireField74_ = 1.0f;
        WireField78_ = 0xFFFFFFFFu;
        if (Kind_ == KindWire2)
            WireField7C_ = 0xFFFFFFFFu;
        return GR_OK;
    }

    if (Kind_ == KindExclusion || Kind_ == KindEngineGlow)
        Exclusion68_ = 0;

    return GR_OK;
}

GENRESULT __thiscall FMaterialImpl::ApplyInitialRenderStates(void* Arg)
{
    if (Kind_ != KindWire2)
        return GR_OK;
    PipelineApplyRsTable(Pipeline_, kWire2Rs);
    PipelineSetTextureStageTexture(Pipeline_, 0, 0);
    PipelineApplyTssTable(Pipeline_, 0, kWire2Tss0);
    return GR_OK;
}

GENRESULT __thiscall FMaterialImpl::Setup()
{
    // LAB_066f1da0..066f1fbd: resolve new groups before dropping old IDs.
    void* ResolvedRecord; // Retail does not preclear the Slot12 output scratch.
    for (unsigned long Index = 0; Index < TextureVerifyCount_; ++Index)
    {
        unsigned char* Entry = (unsigned char*)(TextureBlock_) +
                               Index * kFlmTextureVerifyEntrySize;
        ITextureLibrary2* Library = TextureLibrary_;
        const int OldPlaybackGroupId = *(int*)(Entry + 4);
        int* PlaybackGroupId = (int*)(Entry + 4);
        unsigned long* ResolvedHandle = (unsigned long*)(Entry + 8);
        const char* Name = *(const char* const*)(Entry);
        unsigned char* SingleFrameFlag = Entry + 0x28;
        *ResolvedHandle = 0;
        *PlaybackGroupId = -1;
        *SingleFrameFlag = 1;
        bool bVerified = true;
        if (Name != nullptr)
        {
            struct FTexLibQuery
            {
                const char* Name;
                unsigned long Crc;
            } Query = {Name, DACOM_CRC::GetCRC32(Name)};
            if (Library->FindTextureByCrc(&Query, &ResolvedRecord) >= 0)
            {
                int FrameCount;
                Library->GetFrameCount(ResolvedRecord, &FrameCount);
                *SingleFrameFlag = FrameCount == 1 ? 1 : 0;
                if (Library->CreatePlaybackGroup(ResolvedRecord, PlaybackGroupId) >= 0 &&
                    *SingleFrameFlag != 0)
                    Library->GetFrame(*PlaybackGroupId, 0, ResolvedHandle);
                Library->ReleaseTextureRecord(ResolvedRecord);
            }
            else
            {
                Name = *(const char* const*)(Entry);
                bVerified = std::strcmp(Name, "(None)") == 0;
            }
        }
        if (OldPlaybackGroupId != -1)
            Library->DecRefById(OldPlaybackGroupId); // 066f1e92..066f1ea0
        if (!bVerified)
        {
            char Message[0x2000];
            Name = *(const char* const*)(
                (const unsigned char*)(TextureBlock_) +
                Index * kFlmTextureVerifyEntrySize);
            FlmFormatMessage(Message, "texture verify failed: %s: %s",
                         bInstanceNameSet_ ? InstanceName_ : nullptr, Name);
            FlmFdump(kFdumpNotice, kImplFile, 0x23F, "NOTICE", Message);
        }
    }
    // The zero-count branch also reaches this tail (066f1db9 -> 066f1f0b).
    RecomputeOpacityFlags(); // Full own 066f1f0b..1fbd tail also uses threshold 1.0.
    return GR_OK;
}

bool __thiscall FMaterialImpl::ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value)
{
    // Require the declared type, then check the type-specific range.
    const unsigned char* EntryBytes = (const unsigned char*)(Entry);
    const unsigned long* ValueWords = (const unsigned long*)(Value);
    const unsigned long EntryType = *(const unsigned long*)(EntryBytes + 0x10);
    if (EntryType != ValueWords[0])
        return false;

    unsigned long ComponentCount = 0;
    if (EntryType == 2)
        ComponentCount = 1;
    else if (EntryType == 3)
        ComponentCount = 3;
    else if (EntryType == 4)
        ComponentCount = 4;
    else
        return true; // non-range types: retail always accepts

    const float* Lo = (const float*)(EntryBytes + 0x28); // sub-block 2 ("default")
    const float* Hi = (const float*)(EntryBytes + 0x3C); // sub-block 3 ("range")
    const float* Val = (const float*)(ValueWords + 1);  // Value+4

    for (unsigned long Component = 0; Component < ComponentCount; ++Component)
    {
        // Upper TEST AH,0x41 / JNP rejects ordered <= (upper vs value).
        // Lower TEST AH,0x1 / JZ rejects ordered >=; unordered passes both.
        if ((double)(Val[Component]) >=
            (double)(Hi[Component]) + kFlmVerifyEpsilon)
            return false;
        if ((double)(Val[Component]) <=
            (double)(Lo[Component]) - kFlmVerifyEpsilon)
            return false;
    }
    return true;
}

GENRESULT __thiscall FMaterialImpl::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    // Wire families additionally update the packed colors after storing RGBA.
    if (Kind_ == KindWire || Kind_ == KindWire2)
    {
        void* Raw = GetParameterAddress(Index);
        if (Raw != nullptr)
        {
            const unsigned long* SrcWords = (const unsigned long*)(Value);
            unsigned long* DestWords = (unsigned long*)(Raw);
            switch (SrcWords[0])
            {
            case 3:
                DestWords[2] = SrcWords[3];
                DestWords[1] = SrcWords[2];
                DestWords[0] = SrcWords[1];
                break;
            case 4:
                DestWords[3] = SrcWords[4];
                DestWords[2] = SrcWords[3];
                DestWords[1] = SrcWords[2];
                DestWords[0] = SrcWords[1];
                break;
            case 1:
            case 2:
            case 8:
            case 9:
            case 10:
                DestWords[0] = SrcWords[1];
                break;
            case 5:
            case 7:
                FlmAssignStringRaw((void**)(Raw), SrcWords);
                break;
            case 6:
                FlmAssignInterfaceRaw((void**)(Raw),
                                      (IDAComponent*)(SrcWords[1]));
                break;
            default:
                break;
            }
        }

        const char* ShortTag = *(const char* const*)(
            (const unsigned char*)(Entry) + 4);
        if (ShortTag[0] == 'D' && ShortTag[1] == 'c' && ShortTag[2] == '\0')
        {
            MaterialBlock_[4] = MaterialBlock_[0];
            MaterialBlock_[5] = MaterialBlock_[1];
            MaterialBlock_[6] = MaterialBlock_[2];
            MaterialBlock_[7] = MaterialBlock_[3];
        }
        else if (ShortTag[0] == 'O' && ShortTag[1] == 'c' && ShortTag[2] == '\0')
            RecomputeOpacityFlags(); // FUN_066fb3e0, called before color writeback

        if (Index == 0)
        {
            const float* Components = (const float*)(
                (const unsigned long*)(Value) + 1);
            unsigned long Red;
            unsigned long Green;
            unsigned long Blue;
            unsigned long Alpha;
            if (Kind_ == KindWire)
            {
                // 066fb30e..066fb3a5: Wire explicitly overwrites RGBA for every type.
                WireColorR_ = Components[0];
                WireColorG_ = Components[1];
                WireColorB_ = Components[2];
                WireField74_ = Components[3];
                Alpha = FlmWireColorByte(WireField74_);
                Red = FlmWireColorByte(WireColorR_);
                Green = FlmWireColorByte(WireColorG_);
                Blue = FlmWireColorByte(WireColorB_);
                WireField78_ = (Alpha << 24) | (Red << 16) | (Green << 8) | Blue;
                MaterialBlock_[4] = WireColorR_;
                MaterialBlock_[5] = WireColorG_;
                MaterialBlock_[6] = WireColorB_;
                MaterialBlock_[3] = WireField74_;
            }
            else
            {
                // 066fbfaf..066fc02d: Wire2 packs incoming values without a
                // second RGBA/material writeback. Reduced RGB keeps 5 bits per byte.
                Red = FlmWireColorByte(Components[0]);
                Green = FlmWireColorByte(Components[1]);
                Blue = FlmWireColorByte(Components[2]);
                Alpha = FlmWireColorByte(Components[3]);
                WireField78_ = (Alpha << 24) | (Red << 16) | (Green << 8) | Blue;
                WireField7C_ = ((Red & 0xf8) << 13) | ((Green & 0xf8) << 5) | (Blue >> 3);
            }
            WireGroupIndex_ = 0; // retail Wire+0x7c / Wire2+0x80
        }
        return GR_OK;
    }

    // Colorize has separate color and option-flag storage.
    if (Kind_ == KindColorize)
    {
        const unsigned long* SrcWords = (const unsigned long*)(Value);
        if (Index == 0)
        {
            MaterialBlock_[4] = *(const float*)(&SrcWords[1]);
            MaterialBlock_[5] = *(const float*)(&SrcWords[2]);
            MaterialBlock_[6] = *(const float*)(&SrcWords[3]);
            MaterialBlock_[7] = 1.0f;
            if (SrcWords[0] == 4)
                ColorizeExtraW_ = *(const float*)(&SrcWords[4]);
            return GR_OK;
        }
        if (Index == 0xA)
        {
            // `Index==0xA` (10): writes Value[1] into this+0x6C (ColorizeOptionsFlags_) -- raw
            // dword copy, confirmed flmaterials.dll.txt 11707-11716.
            ColorizeOptionsFlags_ = SrcWords[1];
            return GR_OK;
        }

        void* Raw = GetParameterAddress(Index);
        if (Raw != nullptr)
        {
            unsigned long* DestWords = (unsigned long*)(Raw);
            switch (SrcWords[0])
            {
            case 3:
                DestWords[2] = SrcWords[3];
                DestWords[1] = SrcWords[2];
                DestWords[0] = SrcWords[1];
                break;
            case 4:
                DestWords[3] = SrcWords[4];
                DestWords[2] = SrcWords[3];
                DestWords[1] = SrcWords[2];
                DestWords[0] = SrcWords[1];
                break;
            case 1:
            case 2:
            case 8:
            case 9:
            case 10:
                DestWords[0] = SrcWords[1];
                break;
            case 5:
            case 7:
                FlmAssignStringRaw((void**)(Raw), SrcWords);
                break;
            case 6:
                FlmAssignInterfaceRaw((void**)(Raw), (IDAComponent*)(SrcWords[1]));
                break;
            default:
                break;
            }
        }

        const char* ShortTag = *(const char* const*)(
            (const unsigned char*)(Entry) + 4);
        if (ShortTag[0] == 'D' && ShortTag[1] == 'c' && ShortTag[2] == '\0')
        {
            MaterialBlock_[4] = MaterialBlock_[0];
            MaterialBlock_[5] = MaterialBlock_[1];
            MaterialBlock_[6] = MaterialBlock_[2];
            MaterialBlock_[7] = MaterialBlock_[3];
            return GR_OK;
        }
        if (ShortTag[0] == 'O' && ShortTag[1] == 'c' && ShortTag[2] == '\0')
            RecomputeOpacityFlags();
        return GR_OK;
    }

    void* Raw = GetParameterAddress(Index);
    if (Raw != nullptr)
    {
        const unsigned long* SrcWords = (const unsigned long*)(Value);
        unsigned long* DestWords = (unsigned long*)(Raw);
        switch (SrcWords[0])
        {
        case 3:
            DestWords[2] = SrcWords[3];
            DestWords[1] = SrcWords[2];
            DestWords[0] = SrcWords[1];
            break;
        case 4:
            DestWords[3] = SrcWords[4];
            DestWords[2] = SrcWords[3];
            DestWords[1] = SrcWords[2];
            DestWords[0] = SrcWords[1];
            break;
        case 1:
        case 2:
        case 8:
        case 9:
        case 10:
            DestWords[0] = SrcWords[1];
            break;
        case 5:
        case 7:
            FlmAssignStringRaw((void**)(Raw), SrcWords);
            break;
        case 6:
            FlmAssignInterfaceRaw((void**)(Raw),
                                  (IDAComponent*)(SrcWords[1]));
            break;
        default:
            break;
        }
    }

    // 066f2b21..066f2c9d: the entry and its ShortTag are required by retail.
    const char* ShortTag = *(const char* const*)(
        (const unsigned char*)(Entry) + 4);
    if (ShortTag[0] == 'D' && ShortTag[1] == 'c' && ShortTag[2] == '\0')
    {
        MaterialBlock_[4] = MaterialBlock_[0];
        MaterialBlock_[5] = MaterialBlock_[1];
        MaterialBlock_[6] = MaterialBlock_[2];
        MaterialBlock_[7] = MaterialBlock_[3];
        return GR_OK;
    }
    if (ShortTag[0] == 'O' && ShortTag[1] == 'c' && ShortTag[2] == '\0')
        RecomputeOpacityFlags();
    return GR_OK;
}

void FMaterialImpl::RecomputeOpacityFlags()
{
    // Shared 066f2bf5..066f2c9d and Colorize 066f4c43..066f4ce6.
    // TEST AH,0x5 / JP: equal/greater/unordered -> clear; less -> set.
    Flag64_ = MaterialBlock_[3] < (float)(kCloakColorizeThreshold) ? 1 : 0;
    Flag65_ = 0;
    if (Flag64_ != 0)
        return;
    int ActualFormat = FlmFindPixelFormatIndex(0);
    const unsigned long Handle = TextureBlock_[2];
    if (Handle == 0)
        return;
    if (Pipeline_->GetTextureFormat((void*)(Handle), &ActualFormat) < 0)
        return;
    const unsigned long* ActualDescriptor = FlmPixelFormatDescriptor(ActualFormat);
    Flag65_ = ActualDescriptor[0x38 / 4] == 1 ? 1 : 0;
    // 066f2c4f PUSH shifts the same GetTextureFormat output from ESP+14 to
    // ESP+18 at 066f2c58; Colorize, Setup and Wire have the same stack shift.
    if ((ActualFormat >= 10 && ActualFormat <= 14) ||
        (FlmPixelFormatDescriptor(ActualFormat)[0x48 / 4] & 0xff) != 0)
        Flag64_ = Flag65_ == 0 ? 1 : 0;
    else
        Flag64_ = 0;
}

unsigned long __thiscall FMaterialImpl::GetParameterTable(void** OutBase)
{
    // Mutable table objects are exposed through the existing void** API.
    // FUN_066F2CD0 is the zero-property base. Per-kind Slot27 overrides below
    // supply the retail tables decoded from their disassembly or raw PE bytes.
    if (Kind_ == KindWire || Kind_ == KindWire2)
    {
        void* Table = FlmWireColorDescBase(Kind_ == KindWire2);
        if (OutBase != nullptr)
            *OutBase = Table;
        return 1;
    }
    // Colorize uses its own eleven-property schema.
    if (Kind_ == KindColorize)
    {
        const void* Table = FlmColorizeParamsBase(false);
        if (OutBase != nullptr)
            *OutBase = const_cast<void*>(Table);
        return 11;
    }
    // Cloak DAT_066f37d0 builds the first ten Colorize descriptors: Ambient
    // is type 4, including its fourth component; no Colorize Options entry.
    if (Kind_ == KindCloak)
    {
        const void* Table = FlmColorizeParamsBase(true);
        if (OutBase != nullptr)
            *OutBase = const_cast<void*>(Table);
        return 10;
    }
    if (Kind_ == KindGlass || Kind_ == KindHudIcon || Kind_ == KindExclusion ||
        Kind_ == KindPlanetWater || Kind_ == KindHudAnim || Kind_ == KindEngineGlow)
    {
        const void* Table = FlmGlassParamsBase(Kind_);
        if (OutBase != nullptr)
            *OutBase = const_cast<void*>(Table);
        return 10;
    }
    if (Kind_ == KindNomad)
    {
        const void* Table = FlmNomadParamsBase();
        if (OutBase != nullptr)
            *OutBase = const_cast<void*>(Table);
        return 9;
    }
    if (OutBase != nullptr)
        *OutBase = nullptr;
    return 0;
}

void* __thiscall FMaterialImpl::GetParameterAddress(unsigned long Index)
{
    // Wire families expose their RGBA storage only at index zero.
    if (Kind_ == KindWire || Kind_ == KindWire2)
    {
        if (Index != 0)
            return nullptr;
        return &WireColorR_;
    }
    // Colorize maps each property index to its live storage.
    if (Kind_ == KindColorize)
    {
        void* Result = nullptr;
        switch (Index)
        {
        case 0: Result = &MaterialBlock_[4]; break;   // this+0x30 ("Diffuse" slot, see §28)
        case 1: Result = &MaterialBlock_[0]; break;   // this+0x20 ("Ambient" slot, see §28)
        case 2: Result = &MaterialBlock_[12]; break;  // this+0x50 (Emissive)
        case 3: Result = &MaterialBlock_[8]; break;   // this+0x40 (Specular Color)
        case 4: Result = &MaterialBlock_[16]; break;  // this+0x60 (Specular Power)
        case 5: Result = &MaterialBlock_[3]; break;   // this+0x2C (Ambient alpha / Opacity)
        case 6: Result = TextureBlock_; break;        // *TextureBlock_ (this+0x18)
        case 7: Result = (void*)((uintptr_t)(TextureBlock_) + 0x1c); break;
        case 8: Result = (void*)((uintptr_t)(TextureBlock_) + 0x20); break;
        case 9: Result = (void*)((uintptr_t)(TextureBlock_) + 0x24); break;
        case 10: Result = &ColorizeOptionsFlags_; break; // this+0x6C
        default: Result = nullptr; break;
        }
        return Result;
    }
    // Nomad uses its own property-to-storage map.
    if (Kind_ == KindNomad)
    {
        void* Result = nullptr;
        switch (Index)
        {
        case 0: Result = &NomadDiffuseColor_[0]; break;
        case 1: Result = TextureBlock_; break;
        case 2: Result = (void*)((uintptr_t)(TextureBlock_) + 0x1c); break;
        case 3: Result = (void*)((uintptr_t)(TextureBlock_) + 0x20); break;
        case 4: Result = (void*)((uintptr_t)(TextureBlock_) + 0x24); break;
        case 5: Result = (void*)((uintptr_t)(TextureBlock_) + 0x2c); break;
        case 6: Result = (void*)((uintptr_t)(TextureBlock_) + 0x48); break;
        case 7: Result = (void*)((uintptr_t)(TextureBlock_) + 0x4c); break;
        case 8: Result = (void*)((uintptr_t)(TextureBlock_) + 0x50); break;
        default: Result = nullptr; break;
        }
        return Result;
    }
    // These families share the generic property-to-storage map.
    if (Kind_ == KindCloak || Kind_ == KindEngineGlow || Kind_ == KindExclusion ||
        Kind_ == KindGlass || Kind_ == KindHudIcon || Kind_ == KindPlanetWater ||
        Kind_ == KindHudAnim)
    {
        void* Result = nullptr;
        switch (Index)
        {
        case 0: Result = &MaterialBlock_[4]; break;   // this+0x30
        case 1: Result = &MaterialBlock_[0]; break;   // this+0x20
        case 2: Result = &MaterialBlock_[12]; break;  // this+0x50
        case 3: Result = &MaterialBlock_[8]; break;   // this+0x40
        case 4: Result = &MaterialBlock_[16]; break;  // this+0x60
        case 5: Result = &MaterialBlock_[3]; break;   // this+0x2C
        case 6: Result = TextureBlock_; break;        // *TextureBlock_ (this+0x18)
        case 7: Result = (void*)((uintptr_t)(TextureBlock_) + 0x1c); break;
        case 8: Result = (void*)((uintptr_t)(TextureBlock_) + 0x20); break;
        case 9: Result = (void*)((uintptr_t)(TextureBlock_) + 0x24); break;
        default: Result = nullptr; break;
        }
        return Result;
    }
    return nullptr;
}

void FMaterialImpl::BindUnknown(const char* Why)
{
    FDUMP(kFdumpError, "flmaterials: minimal bind (undocumented type) %s",
          Why != nullptr ? Why : "?");
    SetMaterialBlock(Pipeline_, MaterialBlock_);
}

// Apply this texture entry's coordinates and wrapping to the selected stage.
void FMaterialImpl::ApplyTssFromTextureBlock(unsigned long Stage, const unsigned long* Block)
{
    unsigned long const Coord = (Block[7] >= 4) ? (Block[7] - 4) : 0;
    PipelineSetTextureStageState(Pipeline_, Stage, D3DTSS_TEXCOORDINDEX, Coord);
    PipelineSetTextureStageState(Pipeline_, Stage, D3DTSS_ADDRESSU, Block[8] + 1);
    PipelineSetTextureStageState(Pipeline_, Stage, D3DTSS_ADDRESSV, Block[9] + 1);
}

// Block+0x08, the texture handle handed to the pipeline's SetTexture (vt+0x108) right before
// the stage setup above.
unsigned long FMaterialImpl::TextureHandleAt(const unsigned long* Block) const
{
    return Block[2];
}

bool FMaterialImpl::ContextWantsColorVertex(void* Context) const
{
    if (Context == nullptr)
        return false;
    const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
    if (Source == nullptr)
        return false;
    return Source->Diffuse.Base != nullptr;
}

float FMaterialImpl::CompareField2C() const
{
    return MaterialBlock_[3];
}

void FMaterialImpl::BindGlass(void* Context)
{
    if (CompareField2C() < kGlassRaiseThreshold)
    {
        MaterialBlock_[3] = kGlassRaiseThreshold;
        MaterialBlock_[7] = kGlassRaiseThreshold;
    }
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if (ContextWantsColorVertex(Context))
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 1);
    PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
    ApplyTssFromTextureBlock(0, TextureBlock_);
    PipelineApplyTssTable(Pipeline_, 0, kGlassTss0);
    PipelineApplyTssTable(Pipeline_, 1, kGlassTss1);
    PipelineApplyRsTable(Pipeline_, kGlassRs);
}

void FMaterialImpl::BindNomad()
{
    StageCounter_ = 0;
    PipelineApplyRsTable(Pipeline_, kNomadRs);
    PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 1);
    PipelineSetTextureStageTexture(Pipeline_, StageCounter_, TextureHandleAt(TextureBlock_));
    ApplyTssFromTextureBlock(StageCounter_, TextureBlock_);
    PipelineApplyTssTable(Pipeline_, StageCounter_, kNomadTss0);
    StageCounter_ += 1;
    if (bSecondTexture_ != 0)
    {
        // 066f8ab0..066f8ac1: the second of Nomad's two allocated texture records.
        const unsigned long* Second = TextureBlock_ + (0x2C / 4);
        PipelineSetTextureStageTexture(Pipeline_, StageCounter_, TextureHandleAt(Second));
        ApplyTssFromTextureBlock(StageCounter_, Second);
        PipelineSetTextureStageState(Pipeline_, StageCounter_, D3DTSS_COLOROP, 7);
        PipelineSetTextureStageState(Pipeline_, StageCounter_, D3DTSS_COLORARG1, 2);
        PipelineSetTextureStageState(Pipeline_, StageCounter_, D3DTSS_COLORARG2, 1);
        PipelineSetTextureStageState(Pipeline_, StageCounter_, D3DTSS_ALPHAOP, 4);
        PipelineSetTextureStageState(Pipeline_, StageCounter_, D3DTSS_ALPHAARG1, 2);
        PipelineSetTextureStageState(Pipeline_, StageCounter_, D3DTSS_ALPHAARG2, 1);
        StageCounter_ += 1;
    }
    PipelineApplyTssTable(Pipeline_, StageCounter_, kNomadTssTail);
}

void FMaterialImpl::BindHudIcon(void* Context)
{
    PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
    ApplyTssFromTextureBlock(0, TextureBlock_);
    PipelineApplyTssTable(Pipeline_, 0, kGlassTss0);
    PipelineApplyTssTable(Pipeline_, 1, kGlassTss1);
    if (Context != nullptr && G_FlmEngineOuter != nullptr)
    {
        FNomadContext* Submit = (FNomadContext*)(Context);
        const float* InstanceValues = (const float*)(
            ((IEngine*)(G_FlmEngineOuter))->GetInstanceUserData(Submit->Inst));
        if (InstanceValues != nullptr)
        {
            std::memcpy(&MaterialBlock_[4], InstanceValues, 4 * sizeof(float));
            MaterialBlock_[3] = InstanceValues[4];
        }
    }
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    // TEST AH,0x5 / JNP selects blend for less; unordered follows opaque
    // when Flag64 is clear, just like equal/greater.
    if (!((double)(CompareField2C()) < 1.0) && Flag64_ == 0)
        PipelineApplyRsTable(Pipeline_, kHudIconRsOpaque);
    else
        PipelineApplyRsTable(Pipeline_, kHudIconRsBlend);
}

void FMaterialImpl::BindHudAnim(void* Context)
{
    // LAB_06700070: the enabled flag gates the entire bind, including COLORVERTEX.
    if (HudAnimFlag_ == 0)
        return;
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if (ContextWantsColorVertex(Context))
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 1);
    PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
    ApplyTssFromTextureBlock(0, TextureBlock_);
    PipelineApplyTssTable(Pipeline_, 0, kGlassTss0); // DAT_067030c4
    PipelineApplyTssTable(Pipeline_, 1, kHudAnimTss1); // DAT_067030fc
    PipelineApplyRsTable(Pipeline_, Flag64_ != 0 ? kHudIconRsBlend : kHudIconRsOpaque);
    if (MatAnim_ != nullptr)
    {
        float UvTransform[16];
        if (MatAnim_->FillUvMatrix(UvTransform) != 0)
        {
            PipelineSetTextureStageState(Pipeline_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
            PipelineSetTextureStageTransform(Pipeline_, 0, UvTransform);
        }
    }
}

GENRESULT FMaterialImpl::BindColorize(void* Context)
{
    // FUN_066f4d20: preserve the separately stored Ac.W through either branch.
    float Opacity = ColorizeExtraW_;
    if ((ColorizeOptionsFlags_ & 1) != 0)
    {
        // 066f4ee8..066f4f88: only the fast branch binds our texture/TSS.
        PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
        ApplyTssFromTextureBlock(0, TextureBlock_);
        PipelineApplyTssTable(Pipeline_, 0, kCloakTss); // DAT_06701768
        PipelineApplyTssTable(Pipeline_, 1, kCloakTss);
    }
    else
    {
        if (Context == nullptr)
            return (GENRESULT)(-1);
        FNomadContext* Submit = (FNomadContext*)(Context);
        if (Submit->Candidate == nullptr)
            return (GENRESULT)(-1);

        FScopedComponentReference Batched;
        if (((IMaterial*)(Submit->Candidate))->QueryInterface(
                Iids::BatchedMaterial, &Batched.Object) >= 0)
            ((IBatchedMaterial*)(Batched.Object))->Bind(Context);
        else
            ((IMaterial*)(Submit->Candidate))->Bind(Context);

        // The descriptor owns three typed values. Retail constructs it after
        // binding, then destroys range/default/current before releasing Batched.
        struct FScopedParameterDescriptor
        {
            unsigned long Words[kFlmParamDescSize / sizeof(unsigned long)] = {};
            ~FScopedParameterDescriptor() noexcept(false) { FlmResetDescriptorEntry(Words); }
        } Descriptor;

        const GENRESULT DescriptorResult = ((IMaterial*)(
            Submit->Candidate))->GetParameterDescriptor(5, Descriptor.Words);
        if (DescriptorResult == GR_OK)
        {
            const char* ShortTag = (const char*)(Descriptor.Words[1]);
            // 066f4def..066f4dfc: compare two characters, no length/null test.
            if (ShortTag[0] == 'O' && ShortTag[1] == 'c')
            {
                struct FScopedParameterValue
                {
                    unsigned long Words[5] = {2, 0, 0, 0, 0};
                    ~FScopedParameterValue() noexcept(false) { FlmReleaseTypedValue(Words); }
                } Value;
                ((IMaterial*)(Submit->Candidate))->GetParameterValue(5, Value.Words);
                float CandidateOpacity;
                std::memcpy(&CandidateOpacity, &Value.Words[1], sizeof(CandidateOpacity));
                // FLD float / FMUL float / FSTP float: multiply before rounding.
                Opacity = (float)((double)(CandidateOpacity) *
                                             (double)(Opacity));
            }
        }
    }

    // 066f4f8a..066f4ff8. Bind does not modify the cached Flag64/Flag65.
    MaterialBlock_[3] = Opacity;
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    // TEST AH,0x5 / JP -> plain for equal/greater/unordered; less falls into blend.
    PipelineApplyRsTable(Pipeline_, (double)(Opacity) < kCloakColorizeThreshold
        ? kCloakRsBlend : kCloakRsPlain); // DAT_06701738 / DAT_06701718
    return GR_OK;
}

void FMaterialImpl::BindCloak(void* Context)
{

    // Cloak inherits opacity from the root-instance value and candidate material.
    float Base = FlmRootInstanceValue(Context, 1.0f); // 0x3f800000

    IDAComponent* Candidate = nullptr;
    if (Context != nullptr)
        Candidate = *(IDAComponent**)((unsigned char*)(Context) + 0x1C);

    if (Candidate != nullptr)
    {
        // Bind the candidate through its batched face when available.
        FScopedComponentReference Batched;
        if (Candidate->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        {
            // 066f4093/066f40a1: Context is pushed ahead of the branch, so both slots
            // receive it. This port used to call each with the object alone.
            ((IBatchedMaterial*)(Batched.Object))->Bind(Context);
        }
        else
        {
            ((IMaterial*)(Candidate))->Bind(Context);
        }

        // Read back the candidate material before composing opacity.
        GetMaterialBlock(Pipeline_, MaterialBlock_);
        Base *= MaterialBlock_[3];

        // Batched's destructor balances QI here and on the retail EH path.
    }
    else
    {
        // 066f40ed-06f418e: no Candidate — texture-block-driven TSS setup instead.
        PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
        ApplyTssFromTextureBlock(0, TextureBlock_);
        PipelineApplyTssTable(Pipeline_, 0, kCloakTss);
        PipelineApplyTssTable(Pipeline_, 1, kCloakTss);
    }

    // 066f4190-066f41a1: write the final Base into Opacity, then SetMaterial.
    MaterialBlock_[3] = Base;
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if ((double)(Base) < kCloakColorizeThreshold)
    {
        Flag64_ = 1;
        PipelineApplyRsTable(Pipeline_, kCloakRsBlend);
    }
    else
    {
        Flag64_ = 0;
        PipelineApplyRsTable(Pipeline_, kCloakRsPlain);
    }
}

void FMaterialImpl::BindWire()
{
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if ((double)(WireField74_) < kWireBlendThreshold)
        PipelineApplyRsTable(Pipeline_, kWireRsBlend);
    else
        PipelineApplyRsTable(Pipeline_, kWireRsPlain);
    PipelineSetTextureStageTexture(Pipeline_, 0, 0);
    PipelineApplyTssTable(Pipeline_, 0, kWireTss0);
}

void FMaterialImpl::BindWire2(void* Context)
{
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if (ContextWantsColorVertex(Context))
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 1);
}

void FMaterialImpl::BindExclusion()
{
    PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
    ApplyTssFromTextureBlock(0, TextureBlock_);
    PipelineApplyTssTable(Pipeline_, 0, kExclusionTss0);
    PipelineApplyTssTable(Pipeline_, 1, kExclusionTss1);
    PipelineApplyRsTable(Pipeline_, kExclusionRs);
}

void FMaterialImpl::BindEnvMap(void* Context)
{
    // 066f5af0..5db1: HRESULTs are tested only for QI and Slot22.
    EnvMapFlag_ = 0;
    const char* Name = nullptr;
    IDAComponent* Candidate = Context != nullptr
        ? *(IDAComponent**)((unsigned char*)(Context) + 0x1c)
        : nullptr;
    if (Candidate != nullptr)
    {
        FScopedComponentReference Batched;
        if (Candidate->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        {
            ((IBatchedMaterial*)(Batched.Object))->Bind(Context);
        }
        else
        {
            if ((G_EnvMapGlassGuard & 1) == 0)
            {
                G_EnvMapGlassGuard |= 1;
                G_EnvMapGlassCrc = DACOM_CRC::GetCRC32("GlassMaterial");
                std::atexit(FlmDestroyEnvMapGlassCrc);
            }
            IMaterial* Material = (IMaterial*)(Candidate);
            EnvMapFlag_ = DACOM_CRC::GetCRC32(Material->ClassName()) == G_EnvMapGlassCrc;
            Material->Bind(Context);
        }
        if (EnvMapFlag_ != 0)
        {
            Name = "envmapglass";
        }
        else
        {
            void* Root = *(void**)(Context);
            // 066fc970 returns DAT_06704630, published by 066fca80.
            if (G_FlmEngineOuter != nullptr)
                Root = ((IEngine*)(G_FlmEngineOuter))->GetInstanceRoot(Root);
            void* Found = nullptr;
            if (FlmLookupIdValue((int)((intptr_t)(Root)), &Found))
                Name = (const char*)(Found);
        }
    }

    EnvMapResolvedHandle_ = 0;
    if (Name == nullptr)
    {
        if (TextureLibrary_ != nullptr && ShadowHandle_ != 0)
            TextureLibrary_->ReleaseTextureRecord(
                (void*)(ShadowHandle_));
        ShadowHandle_ = 0;
        EnvMapTexNameCrc_ = 0;
    }
    else
    {
        const unsigned long NameCrc = DACOM_CRC::GetCRC32(Name);
        if (NameCrc != EnvMapTexNameCrc_)
        {
            if (TextureLibrary_ != nullptr && ShadowHandle_ != 0)
                TextureLibrary_->ReleaseTextureRecord(
                    (void*)(ShadowHandle_));
            ShadowHandle_ = 0;
            EnvMapTexNameCrc_ = NameCrc;
            if (NameCrc != 0)
            {
                struct FTextureNameQuery
                {
                    const char* Name;
                    unsigned long Crc;
                } Query = {Name, DACOM_CRC::GetCRC32(Name)};
                TextureLibrary_->FindTextureByCrc(&Query,
                    (void**)(&ShadowHandle_));
            }
        }
    }
    if (ShadowHandle_ != 0)
    {
        unsigned long Out[5];
        if (TextureLibrary_->GetTextureFrame(
                (void*)(ShadowHandle_), 0, Out) >= 0)
            EnvMapResolvedHandle_ = Out[0];
    }
    if (EnvMapFlag_ == 0 && EnvMapResolvedHandle_ != 0)
    {
        unsigned long Stage = 0;
        unsigned long Probe = 1;
        PipelineGetTextureStageState(Pipeline_, 0, 1, &Probe);
        if (Probe != 1)
        {
            Stage = 1;
            PipelineGetTextureStageState(Pipeline_, 1, 1, &Probe);
            if (Probe != 1)
                return;
        }
        PipelineApplyTssTable(Pipeline_, Stage, kEnvMapTss0);
        PipelineApplyTssTable(Pipeline_, Stage + 1, kEnvMapTss1);
        PipelineSetTextureStageTexture(Pipeline_, Stage, EnvMapResolvedHandle_);
        PipelineSetTextureStageState(Pipeline_, Stage, D3DTSS_TEXCOORDINDEX, 0x30000);
        PipelineSetRenderState(Pipeline_, D3DRS_LOCALVIEWER, 1);
        PipelineSetRenderState(Pipeline_, D3DRS_NORMALIZENORMALS, 1);
    }
}

namespace
{
    struct FShadowBatchedReference
    {
        void* Object = nullptr;
        ~FShadowBatchedReference() noexcept(false)
        {
            if (Object != nullptr)
            {
                ((IDAComponent*)(Object))->Release();
                Object = nullptr;
            }
        }
    };
}

GENRESULT FMaterialImpl::BindShadow(void* Context)
{
    // LAB_066fa3f0: cache only on the first bind; failure does not reset the latch.
    if (!bShadowTextureInitialized_)
    {
        ShadowTextureLibrary_ = TextureLibrary_;
        bShadowTextureInitialized_ = true;
        if (ShadowTextureRecord_ == nullptr)
        {
            struct FTextureQuery { const char* Name; unsigned long Crc; };
            FTextureQuery Query = {"efxshadow", DACOM_CRC::GetCRC32("efxshadow")};
            if (ShadowTextureLibrary_->FindTextureByCrc(&Query, &ShadowTextureRecord_) < 0)
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                    "E:\\FL\\Scratch\\Source\\FLMaterials\\shadows_inl.h", 0x24,
                    "couldnt find texture 'efxshadow' (from efx.txm?)");
            else
            {
                unsigned long Frame[5];
                if (ShadowTextureLibrary_->GetTextureFrame(ShadowTextureRecord_, 0, Frame) >= 0)
                    ShadowResolvedHandle_ = Frame[0];
            }
        }
    }
    if (Context != nullptr)
    {
        FNomadContext* Submit = (FNomadContext*)(Context);
        if (Submit->Candidate != nullptr)
        {
            int LayerCount = (int)((intptr_t)(Context));
            ((IMaterial*)(Submit->Candidate))->GetPassCount(&LayerCount);
            if ((unsigned long)(LayerCount) != ShadowLayerCount_)
                ShadowLayerCount_ = (unsigned long)(LayerCount);
            FShadowBatchedReference Batched;
            if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
                ((IBatchedMaterial*)(Batched.Object))->Bind(Context);
            else
                ((IMaterial*)(Submit->Candidate))->Bind(Context);
        }
    }
    return (GENRESULT)(-1);
}

GENRESULT FMaterialImpl::DrawShadow(void* Context, unsigned long Prim, unsigned long Start,
                                    unsigned long Count, unsigned long IbStart,
                                    unsigned long IndexSpan, unsigned long Flags)
{
    // FUN_066fa5c0. Only the held batched reference is EH-owned; state resets are normal-path work.
    int SavedCull;
    Pipeline_->GetRenderState(D3DRS_ZFUNC, &SavedCull);
    const unsigned long Stage = ShadowLayerCount_ < 2 ? 1 : 0;
    Pipeline_->SetTextureStageTexture((int)(Stage), (void*)(ShadowResolvedHandle_));
    if (Stage == 1)
    {
        IRenderPipeline* TablePipeline = Pipeline_;
        for (unsigned long Index = 0; kShadowTss1[Index] != 0; Index += 2)
            TablePipeline->SetTextureStageState(1, kShadowTss1[Index], kShadowTss1[Index + 1]);
    }
    Pipeline_->SetTextureStageState(Stage, D3DTSS_ADDRESSU, 3);
    Pipeline_->SetTextureStageState(Stage, D3DTSS_ADDRESSV, 3);
    Pipeline_->SetTextureStageState(Stage, D3DTSS_TEXCOORDINDEX, 0x20000);
    Pipeline_->SetTextureStageState(Stage, D3DTSS_TEXTURETRANSFORMFLAGS, 0x103);
    FNomadContext* Submit = (FNomadContext*)(Context);
    GENRESULT Result = (GENRESULT)(-1);
    if (Submit != nullptr && Submit->Candidate != nullptr)
    {
        FShadowBatchedReference Batched;
        if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
            Result = ((IBatchedMaterial*)(Batched.Object))->Submit(Context, Prim, Start,
                Count, IbStart, IndexSpan, Flags);
        else
            Result = ((IMaterial*)(Submit->Candidate))->Submit(Context, Prim, Start,
                Count, IbStart, IndexSpan, Flags);
    }
    if (ShadowLayerCount_ > 1)
    {
        Pipeline_->SetRenderState(D3DRS_LIGHTING, 0);
        Pipeline_->SetRenderState(D3DRS_ZFUNC, 4);
        bool bDrawn = false;
        if ((Flags & 0x100) != 0)
        {
            const unsigned long IbBase = *(const unsigned long*)(
                (unsigned char*)(Submit->IbWrapper) + 4);
            Result = RpDraw_->DrawIndexedPrimitive(Prim, Start, Count,
                IbBase + IbStart, IndexSpan);
            bDrawn = Result >= 0;
        }
        if (!bDrawn)
        {
            Result = (GENRESULT)(-1);
            if (Submit->VbListing != nullptr)
            {
                Result = Pipeline_->DrawIndexedPrimitiveVb(Prim, Submit->VbListing->VertexBuffer,
                    Submit->VbListing->BaseVertex + Start, Count,
                    (void*)((uintptr_t)(Submit->Indices) + IbStart * 2),
                    IndexSpan, 0);
            }
            if (Result < 0 && Submit->Mesh != nullptr && Count != 0 && IndexSpan != 0)
            {
                const unsigned long RequestedFvf = Submit->Mesh->SrcFvf;
                FVertexMemoryLock LockOut;
                Result = VertexBufferManager_->LockVertexMemory(
                    RequestedFvf, Count, &LockOut);
                if (Result >= 0)
                {
                    const unsigned long GrantedFvf = LockOut.Listing.Fvf;
                    const unsigned long Stride = (unsigned long)(FvfStride(GrantedFvf));
                    void* DestBase = (void*)((uintptr_t)(LockOut.Vertices) - Stride * Start);
                    VertexBufferManager_->CopyVertexData(DestBase,
                        GrantedFvf, Submit->Mesh,
                        Start, Count);
                    VertexBufferManager_->UnlockVertexMemory(&LockOut);
                    Result = Pipeline_->DrawIndexedPrimitiveVb(Prim, LockOut.Listing.VertexBuffer, LockOut.Listing.BaseVertex,
                        Count, (void*)((uintptr_t)(Submit->Indices) + IbStart * 2),
                        IndexSpan, 0);
                }
            }
        }
    }
    Pipeline_->SetTextureStageState(Stage, D3DTSS_TEXCOORDINDEX, Stage);
    Pipeline_->SetTextureStageState(Stage, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    Pipeline_->SetRenderState(D3DRS_ZFUNC, SavedCull);
    Pipeline_->SetRenderState(D3DRS_LIGHTING, 1);
    return Result;
}

GENRESULT FMaterialImpl::ShadowCandidateDispatch(void* Context)
{
    if (Context == nullptr)
        return (GENRESULT)(-1);
    FNomadContext* Submit = (FNomadContext*)(Context);
    if (Submit->Candidate == nullptr)
        return (GENRESULT)(-1);
    FShadowBatchedReference Batched;
    if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        return ((IBatchedMaterial*)(Batched.Object))->Teardown(Context);
    return ((IMaterial*)(Submit->Candidate))->Teardown(Context);
}

void FMaterialImpl::TeardownCandidateIgnoringResult(void* Context)
{
    // 066f42e0 / 066f5010 / 066f6240: optional candidate, result discarded.
    if (Context == nullptr)
        return;
    FNomadContext* Submit = (FNomadContext*)(Context);
    if (Submit->Candidate == nullptr)
        return;
    FShadowBatchedReference Batched;
    if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        ((IBatchedMaterial*)(Batched.Object))->Teardown(Context);
    else
        ((IMaterial*)(Submit->Candidate))->Teardown(Context);
}

GENRESULT FMaterialImpl::SubmitCandidateGeometry(void* Context, unsigned long PrimType,
                                                unsigned long Start, unsigned long Count,
                                                unsigned long IbStart,
                                                unsigned long IndexSpan,
                                                unsigned long Flags)
{
    // 066f4220: caller owns the optional candidate checks and material flag.
    FNomadContext* Submit = (FNomadContext*)(Context);
    FShadowBatchedReference Batched;
    if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        return ((IBatchedMaterial*)(Batched.Object))->Submit(Context, PrimType, Start,
            Count, IbStart, IndexSpan, Flags);
    return ((IMaterial*)(Submit->Candidate))->Submit(Context, PrimType, Start,
        Count, IbStart, IndexSpan, Flags);
}

void FMaterialImpl::BindEngineGlow(void* Context)
{
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if (ContextWantsColorVertex(Context))
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 1);
    PipelineApplyTssTable(Pipeline_, 0, kEngineGlowTss);
    PipelineApplyRsTable(Pipeline_, kEngineGlowRs);
    SetMaterialBlock(Pipeline_, MaterialBlock_);
}

void FMaterialImpl::BindVisibleCounter()
{
}

void FMaterialImpl::BindBackdrop(void* Context)
{
    if (Context == nullptr)
        return;
    FNomadContext* Submit = (FNomadContext*)(Context);
    if (Submit->Candidate == nullptr)
        return;
    FShadowBatchedReference Batched;
    if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        ((IBatchedMaterial*)(Batched.Object))->Bind(Context);
    else
        ((IMaterial*)(Submit->Candidate))->Bind(Context);
}

GENRESULT FMaterialImpl::TeardownBackdrop(void* Context)
{
    // FUN_066f2e30 has no Context/Candidate null checks.
    FNomadContext* Submit = (FNomadContext*)(Context);
    FShadowBatchedReference Batched;
    if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
        return ((IBatchedMaterial*)(Batched.Object))->Teardown(Context);
    return ((IMaterial*)(Submit->Candidate))->Teardown(Context);
}

GENRESULT FMaterialImpl::DrawBackdrop(void* Context, unsigned long Prim, unsigned long Start,
                                      unsigned long Count, unsigned long IbStart,
                                      unsigned long IndexSpan, unsigned long Flags)
{
    if (Context == nullptr)
        return (GENRESULT)(-1);
    FNomadContext* Submit = (FNomadContext*)(Context);
    if (Submit->Candidate == nullptr)
        return (GENRESULT)(-1);

    // 066f2ef9..2f93: capture each pipeline anew; arm only after the corresponding force.
    FFlmRenderStateRestore Depth(Pipeline_, D3DRS_ZENABLE);
    Depth.Pipeline->SetRenderState(Depth.State, 0);
    Depth.bActive = true;
    FFlmRenderStateRestore DepthWrite(Pipeline_, D3DRS_ZWRITEENABLE); // formerly Lighting
    DepthWrite.Pipeline->SetRenderState(DepthWrite.State, 0);
    DepthWrite.bActive = true;
    FFlmRenderStateRestore DepthFunction(Pipeline_, D3DRS_ZFUNC); // formerly Cull
    DepthFunction.Pipeline->SetRenderState(DepthFunction.State, 8);
    DepthFunction.bActive = true;
    GENRESULT Result;
    {
        FShadowBatchedReference Batched;
        if (((IDAComponent*)(Submit->Candidate))->QueryInterface(Iids::BatchedMaterial, &Batched.Object) >= 0)
            Result = ((IBatchedMaterial*)(Batched.Object))->Submit(Context, Prim, Start,
                Count, IbStart, IndexSpan, Flags);
        else
            Result = ((IMaterial*)(Submit->Candidate))->Submit(Context, Prim, Start,
                Count, IbStart, IndexSpan, Flags);
    }
    // 066f3005..3055: reverse construction order on normal and EH paths.
    DepthFunction.Restore();
    DepthWrite.Restore();
    Depth.Restore();
    return Result;
}

void FMaterialImpl::BindPlanetWater(void* Context)
{
    SetMaterialBlock(Pipeline_, MaterialBlock_);
    if (ContextWantsColorVertex(Context))
        PipelineSetRenderState(Pipeline_, D3DRS_COLORVERTEX, 1);
    PipelineSetTextureStageTexture(Pipeline_, 0, TextureHandleAt(TextureBlock_));
    ApplyTssFromTextureBlock(0, TextureBlock_);
    PipelineApplyTssTable(Pipeline_, 0, kPlanetTss0);
    PipelineApplyTssTable(Pipeline_, 1, kPlanetTss1);
    if (Flag64_ != 0)
        PipelineApplyRsTable(Pipeline_, kPlanetRsBlend);
    else
        PipelineApplyRsTable(Pipeline_, kPlanetRsPlain);
    if (MatAnim_ != nullptr)
    {
        // Animated water supplies a UV matrix and enables the corresponding texture transform.
        float UvTransform[16];
        std::memset(UvTransform, 0, sizeof(UvTransform));
        if (MatAnim_->FillUvMatrix(UvTransform) != 0)
        {
            PipelineSetTextureStageState(Pipeline_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
            PipelineSetTextureStageTransform(Pipeline_, 0, UvTransform);
        }
    }
}

IMaterial* FlmCreateMaterial(const FMaterialSpec& Spec)
{
    return new (std::nothrow) FMaterialImpl(Spec);
}
