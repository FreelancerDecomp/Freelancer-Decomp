// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "misc_util.h"
#include "msvcrt_compat.h" // MsvcrtWtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "anim_script_set.h"

#include "DALib.h"
#include "common_archetype.h"
#include "common_csys.h" // Acquire3DMathEngine
#include "common_goods.h"
#include "common_misc.h"
#include "common_reputation.h"
#include "common_universe.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "ids_strings.h"
#include "render/shading.h" // ITextureLibrary2 (FUN_0041c7e0 / FUN_0041c830)
#include "x86math.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace
{
    char const* const kMiscUtilCpp = "E:\\FL\\Scratch\\Source\\Client\\cmn\\MiscUtil.cpp";
    char const* const kFailedToLoadIds = "%s(%d) : *** WARNING: Failed to load ids: %u";
}

FmtStr::Val const* FmtStrFindVal(FmtStr const* Fmt, unsigned char Index, unsigned short Code)
{
    // 0041c940: walk from [this], compare word [node+8] with Code and byte [node+0xa] with
    // Index; null when the list runs out.
    for (FmtStr::Val const* Node = Fmt->Head; Node != nullptr; Node = Node->Next)
    {
        if (Node->TypeTag == Code && Node->Flags == Index)
            return Node;
    }
    return nullptr;
}

wchar_t const* MiscUtilLoadIdsCopy(wchar_t** Out, unsigned Ids)
{
    // 0041c770.
    wchar_t Text[0x400]; // local_800
    if (GetIdsStringWWithRawFallback(Ids, Text, 0x400) == 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to find string resource %d\n", kMiscUtilCpp,
              0xf1, Ids);
    }
    return malloc_wstrcpy(Out, Text);
}

int MiscUtilLoadTextureLibrary(char const* File)
{
    // 0041c7e0. The out slot of TextureLib slot 5 is the local the prologue's PUSH ECX made; it
    // is not initialised first, so a failing load hands back whatever slot 5 left there. The
    // file system is not checked for null.
    IFileSystem* FileSystem = nullptr;
    DAEngine_create_filesystem(File, &FileSystem, "UTF");
    int Group;
    DALib::TextureLib->LoadTextureGroup(FileSystem, &Group);
    DAEngine_release_filesystem(&FileSystem);
    return Group;
}

void MiscUtilReleaseTextureLibrary(int Group)
{
    // 0041c830: TextureLib slot 7 (+0x1c), the result dropped.
    DALib::TextureLib->GarbageCollect(Group);
}

int MiscUtilLoadMaterialLibrary(char const* File)
{
    // 0041c850. The file system lands in the argument's own stack slot; the group local is -1
    // before the load. Implementation 0 lets Common pick the file system. None of the results
    // is checked.
    IFileSystem* FileSystem = nullptr;
    unsigned long Group = 0xffffffffu;
    DAEngine_create_filesystem(File, &FileSystem, nullptr);
    DALib::MaterialLib->LoadFromFileSystem(FileSystem, &Group);            // slot 8 (+0x20)
    DALib::MaterialLib->ApplyVerifyGroup(static_cast<int>(Group), nullptr, // slot 11 (+0x2c)
                                         reinterpret_cast<void*>(0x3f800000u));
    DALib::MaterialLib->ResolveTexturesGroup(static_cast<int>(Group), 0.0f); // slot 12 (+0x30)
    DAEngine_release_filesystem(&FileSystem);
    return static_cast<int>(Group);
}

void MiscUtilAppendIds(wchar_t** Cursor, wchar_t* End, unsigned Ids)
{
    // 0041d5b0. The character count is the SAR'd byte distance to End (0041d5ca).
    int const Copied = static_cast<int>(
        GetIdsStringWWithRawFallback(Ids, *Cursor, static_cast<int>(End - *Cursor)));
    if (Copied == 0)
        FDUMP(0x100002, kFailedToLoadIds, kMiscUtilCpp, 0x18c, Ids);
    *Cursor += Copied;
    if (End == *Cursor)
    {
        // 0041d60f: step back, terminate, step forward again.
        *Cursor -= 1;
        **Cursor = L'\0';
        *Cursor += 1;
    }
}

void FormatGroupedNumber(wchar_t* Out, int Value, int PrefixIds)
{
    // 004779a0.
    if (PrefixIds < 0)
        GetIdsStringWWithRawFallback(0x2140, Out, 0xff);
    else if (PrefixIds != 0)
        GetIdsStringWWithRawFallback(static_cast<unsigned>(PrefixIds), Out, 0xff);
    else
        Out[0] = L'\0';

    if (Value == 0)
    {
        wcscat(Out, L"0"); // DAT_005d0ed8
        return;
    }

    int Divisor = 1000000;
    bool bStarted = false;
    do
    {
        // 00477a20: signed compare (JGE).
        if (Value >= Divisor || bStarted)
        {
            int const Group = Value / Divisor;
            wchar_t Text[64]; // local_80, 0x80 bytes
            // u"%03d" 005d0ecc once a group has been written, u"%d" 005d0ec4 before.
            swprintf(Text, 64, bStarted ? L"%03d" : L"%d", Group);
            wcscat(Out, Text);
            if (Divisor > 1)
                wcscat(Out, L","); // DAT_005d0ec0
            Value -= Group * Divisor;
            bStarted = true;
        }
        Divisor /= 1000;
    } while (Divisor > 0);
}

void MiscUtilAppendRepInstanceName(int RepId, wchar_t** Cursor, wchar_t* End)
{
    // 0041d440. End (the third argument) is never read: the copy is an unbounded wcscpy.
    (void)End;

    FmtStr Name1;              // [ESP+0x28]
    FmtStr Name2;              // [ESP+0x10]
    wchar_t Rebuilt[0x18];     // [ESP+0x40]
    // local_70 is not written before the call; on a miss GetName leaves it untouched and
    // retail reads whatever the stack held. Null steers that case into the rebuild branch.
    wchar_t const* DisplayName = nullptr;

    Reputation::Vibe::GetName(RepId, Name1, Name2, DisplayName);
    if (DisplayName == nullptr || DisplayName[0] == L'\0')
    {
        FmtStrSprintf(&Name2, Rebuilt, 0x18);
        Reputation::Vibe::SetName(RepId, Name1, Name2, Rebuilt);
        DisplayName = Rebuilt;
    }

    size_t const Length = wcslen(DisplayName);
    wcscpy(*Cursor, DisplayName);
    *Cursor += Length;
}

int FmtStrSprintf(FmtStr const* Fmt, wchar_t* Dest, int DestChars)
{
    // 0041c970.
    wchar_t* const End = Dest + DestChars;
    if (End == Dest)
        return 0;
    *Dest = L'\0';
    if (Fmt->MadLibId == 0)
        return 0;

    bool bPercent = false;                  // local_res3
    wchar_t Number[32];                     // [ESP+0x28] -> FUN_004779a0
    wchar_t Digits[64];                     // [ESP+0x68]
    wchar_t Template[0x1000];               // [ESP+0xe8]
    GetIdsStringWWithRawFallback(Fmt->MadLibId, Template, 0x1000); // result unused (0041c9de)

    wchar_t* Cursor = Dest;
    wchar_t const* Source = Template;
    for (;;)
    {
        if (Cursor >= End) // 0041c9fe JNC
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: FmtStr::sprintf(): mad_lib %u (%S) was too long, please shorten",
                  kMiscUtilCpp, 0x1d3, Fmt->MadLibId, Dest);
            break;
        }

        wchar_t const Code = *Source;
        if (Code == L'\0')
            break;
        ++Source;

        if (Code == L'%')
        {
            bPercent = true;
            continue;
        }

        if (!bPercent)
        {
            *Cursor = Code;
            ++Cursor;
            if (Cursor == End)
                break;
            *Cursor = L'\0';
            continue;
        }

        // 0041ca53: "%<code><index digits>[v<digit>]".
        bPercent = false;
        unsigned DigitCount = 0;
        wchar_t Next = *Source;
        if (Next >= L'0')
        {
            for (;;)
            {
                if (Next > L'9')
                    break;
                Digits[DigitCount] = Next;
                ++DigitCount;
                // 0041ca72: the 63rd digit is stored but Source is not stepped past it.
                if (DigitCount >= 0x3f)
                    break;
                Next = Source[1];
                ++Source;
                if (Next < L'0')
                    break;
            }
        }
        Digits[DigitCount] = L'\0';
        unsigned char const Index = static_cast<unsigned char>(MsvcrtWtoi(Digits));

        int Variant = -1; // ESI
        if (*Source == L'v')
        {
            // 0041caa7: the low byte of the next character, compared signed.
            signed char const VariantChar = static_cast<signed char>(Source[1]);
            if (VariantChar >= '0' && VariantChar <= '9')
            {
                Source += 2;
                Variant = VariantChar - '0';
            }
        }

        switch (Code)
        {
        case L's': // 0041cae0
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            unsigned const Ids = Val->Value;
            if (Ids == 2)
                break;
            MiscUtilAppendIds(&Cursor, End, Ids);
            break;
        }

        case L'd': // 0041cb8c
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            FormatGroupedNumber(Number, (int)Val->Value, 0x1f);
            size_t const Length = wcslen(Number);
            if (Cursor + Length < End) // 0041cbe1 JNC
            {
                wcscat(Cursor, Number);
                Cursor += Length;
            }
            break;
        }

        case L'S': // 0041cc0d
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            Universe::ISystem const* System =
                Universe::get_system(Val->Value);
            if (System == nullptr)
                break;
            MiscUtilAppendIds(&Cursor, End, (unsigned)System->GetStridName()); // vtable +0xc
            break;
        }

        case L'B': // 0041cc59
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            Universe::IBase const* Base =
                Universe::get_base(Val->Value);
            if (Base == nullptr)
                break;
            // Retail calls BASE vtable +0x8 (Common 0629fb90: MOV EAX,[this+0xc] -- strid_name).
            // The port's Universe::IBase is a plain record, so the field is read directly.
            MiscUtilAppendIds(&Cursor, End, static_cast<unsigned>(Base->StridName));
            break;
        }

        case L'R': // 0041cca0
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            MiscUtilAppendRepInstanceName((int)Val->Value,
                                          &Cursor, End);
            break;
        }

        case L'F': // 0041cce0
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            unsigned const Name =
                Reputation::get_name(Val->Value);
            if (Name == 0xffffffffu)
                break;
            if (Variant < 0)
            {
                MiscUtilAppendIds(&Cursor, End, Name);
                break;
            }
            // 0041cd9c: the faction's variant name table.
            unsigned const Offset = Name - 0x300ee;
            if (Offset >= 100)
                break;
            MiscUtilAppendIds(&Cursor, End, static_cast<unsigned>(Variant * 100) + Offset + 0x503e8);
            break;
        }

        case L'G': // 0041cdba
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            GoodInfo const* Good =
                GoodList::find_by_id(Val->Value);
            if (Good == nullptr)
                break;
            MiscUtilAppendIds(&Cursor, End, static_cast<unsigned>(Good->IdsName)); // [+0x88]
            break;
        }

        case L'N': // 0041cdfe
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            FmtStr::NavMarker const* Marker = Val->Marker;
            if (Marker == nullptr)
                break;
            MiscUtilAppendIds(&Cursor, End, Marker->Data[1]); // [+4]
            break;
        }

        case L'Z': // 0041ce39
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            unsigned const ZoneId = Val->Value;
            Universe::IZone const* Zone = Universe::get_zone(ZoneId);
            if (Variant < 0)
            {
                if (Zone != nullptr)
                {
                    MiscUtilAppendIds(&Cursor, End, static_cast<unsigned>(Zone->GetName())); // +0x84
                    break;
                }
                // 0041ce87: no zone -- a grid cell id: ids 0x331b, ids 1, column, row.
                if (ZoneId > 0x3f)
                    break;
                MiscUtilAppendIds(&Cursor, End, 0x331b);
                MiscUtilAppendIds(&Cursor, End, 1);
                MiscUtilAppendIds(&Cursor, End, (ZoneId & 7) + 0x514);
                MiscUtilAppendIds(&Cursor, End, ((ZoneId >> 3) & 7) + 0x506);
                break;
            }
            if (Zone != nullptr)
            {
                // 0041d017.
                unsigned const Offset = static_cast<unsigned>(Zone->GetName()) - 0x3fc58;
                if (Offset >= 0xc7)
                    break;
                MiscUtilAppendIds(&Cursor, End, static_cast<unsigned>(Variant * 200) + Offset + 0x50fa1);
                break;
            }
            // 0041d094. The FDUMP severity here is built by FUN_00411c50(0x10000, 2), which is
            // the same 0x100002 every other warning passes.
            if (ZoneId > 0x3f)
                break;
            MiscUtilAppendIds(&Cursor, End, static_cast<unsigned>(Variant * 200) + 0x50fa0);
            MiscUtilAppendIds(&Cursor, End, 1);
            MiscUtilAppendIds(&Cursor, End, (ZoneId & 7) + 0x514);
            MiscUtilAppendIds(&Cursor, End, ((ZoneId >> 3) & 7) + 0x506); // FUN_0041d5b0 call
            break;
        }

        case L'!': // 0041d267
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr || Val->Subtree == nullptr)
                break;
            int const Written = FmtStrSprintf(Val->Subtree, Cursor,
                                              static_cast<int>(End - Cursor));
            Cursor += Written;
            break;
        }

        case L'I': // 0041d2b3
        case L'L': // 0041d301
        {
            FmtStr::Val const* Val = FmtStrFindVal(Fmt, Index, Code);
            if (Val == nullptr)
                break;
            unsigned const Id = Val->Value;
            if (Id >= 100)
                break;
            // SETLE/DEC/AND: the variant counts only when positive.
            unsigned const Page = Variant > 0 ? static_cast<unsigned>(Variant) : 0u;
            unsigned const Base = Code == L'I' ? 0x507d0u : 0x50bb8u;
            MiscUtilAppendIds(&Cursor, End, Page * 100 + Id + Base);
            break;
        }

        default: // table entry 0xc (caseD_22): the escape is consumed, nothing is written
            break;
        }
    }

    // 0041d37c.
    if (Cursor >= End)
        Cursor = End - 1;
    *Cursor = L'\0';
    return static_cast<int>(Cursor - Dest);
}

void* MiscUtilLoadEngineInstance(char const* File, IFileSystem* /*Data*/, FAnimScriptSet* Scripts,
                                 void* /*Unused*/,
                                 Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> const*
                                     MaterialPaths,
                                 std::vector<Archetype::FCliMaterialEntry*>* Entries,
                                 long* OutHandle)
{
    // FUN_0041c540. 0041c546..0041c572: Engine +0x44 on {File, CRC32(File)}.
    void* Instance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    FEngineName Key;
    Key.Text = File;
    Key.Crc = DACOM_CRC::GetCRC32(File);
    void* const Found = DALib::Engine->FindArchetype(&Key);

    if (Found != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
    {
        // 0041c6fa..0041c722: already loaded -- *OutHandle, Engine +0x5c (Handle, 0), +0x3c.
        if (OutHandle != nullptr)
            *OutHandle = reinterpret_cast<long>(Found);
        void* const Created = DALib::Engine->CreateInstance(Found, nullptr);
        DALib::Engine->ReleaseHandle(Found);
        // 0041c725..0041c763: the scripts through a fresh file system (retail parks it in the
        // OutHandle argument slot and does not test it).
        if (Scripts != nullptr)
        {
            IFileSystem* Filesys = nullptr;
            DAEngine_create_filesystem(File, &Filesys, nullptr);
            Scripts->Load(reinterpret_cast<unsigned long>(DALib::Anim->OpenScript(Filesys)));
            DAEngine_release_filesystem(&Filesys);
        }
        return Created;
    }

    // 0041c57d..0041c594: File's own file system (retail parks it in the File argument slot).
    IFileSystem* Filesys = nullptr;
    DAEngine_create_filesystem(File, &Filesys, nullptr);
    if (Filesys == nullptr)
    {
        // 0041c6e0..0041c6f5 -> 0041c5e9.
        FDUMP(0x100002, "%s(%d) : *** WARNING: file (%s) does not exist.", kMiscUtilCpp, 0xbe,
              File);
        return Instance;
    }

    // 0041c59a..0041c5b9: the file's own material entry, then at most 5 listed extra files.
    Archetype::FCliMaterialEntry* FileEntry =
        Archetype::ResolveMaterialEntryFromFs(Filesys, File, true, 0xffffffffu);
    Archetype::FCliMaterialEntry* ExtraEntries[5];
    int const ExtraCount = Archetype::ResolveMaterialEntryList(MaterialPaths, ExtraEntries, 5);

    // 0041c5bf..0041c5fe: the archetype; a failure leaves the file system and the entries as
    // they are.
    long Handle = 0;
    if (!Archetype::Load(Filesys, &Handle, nullptr))
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: couldnt load archetype (%s) .", kMiscUtilCpp,
              0x96, File);
        return Instance;
    }

    // 0041c5ff..0041c62e: *OutHandle, Engine +0x5c (Handle, 0), +0x3c.
    if (OutHandle != nullptr)
        *OutHandle = Handle;
    Instance = DALib::Engine->CreateInstance(reinterpret_cast<void*>(Handle), nullptr);
    DALib::Engine->ReleaseHandle(reinterpret_cast<void*>(Handle));

    // 0041c631..0041c64e: Anim +0xc on the file system into FUN_00555bf0.
    if (Scripts != nullptr)
        Scripts->Load(reinterpret_cast<unsigned long>(DALib::Anim->OpenScript(Filesys)));
    // 0041c653..0041c658.
    DAEngine_release_filesystem(&Filesys);

    // 0041c65e..0041c699: the entries appended to Entries (FUN_00537260 at its end), the
    // file's own first -- or 0041c6a5..0041c6cd: each released through FUN_0041b030.
    if (Entries != nullptr)
    {
        Entries->push_back(FileEntry);
        for (int Index = 0; Index < ExtraCount; ++Index)
            Entries->push_back(ExtraEntries[Index]);
        return Instance;
    }
    Archetype::ReleaseMaterialEntrySlot(&FileEntry);
    for (int Index = 0; Index < ExtraCount; ++Index)
        Archetype::ReleaseMaterialEntrySlot(&ExtraEntries[Index]);
    return Instance;
}

namespace
{
    // DAT_00616720 -- this file's 3DMathEngine, acquired (FUN_005b73e0) by FUN_0041c8d0 on first
    // use (0x0041c8e1..0x0041c8ef).
    I3DMathEngine* G_MiscUtilMath = nullptr;
}

// FUN_0041c8d0. 0x0041c8d8 Camera slot 16 (+0x40) the view transform; 0x0041c8f4..0x0041c902
// TransformXform (+0x28) (out, *View, *Center); 0x0041c91d..0x0041c92a Camera slot 15 (+0x3c)
// (&out, Radius); 0x0041c92f `CMP EAX,3; SETGE`.
bool MiscUtilSphereInView(ICamera* Camera, float const* Center, float Radius)
{
    float const* const View = Camera->GetViewTransform();
    if (G_MiscUtilMath == nullptr)
        G_MiscUtilMath = Acquire3DMathEngine();
    FVector InView;
    G_MiscUtilMath->TransformXform(InView, *reinterpret_cast<FTransform const*>(View),
                                   *reinterpret_cast<FVector const*>(Center));
    float Local[3];
    Local[0] = InView.X;
    Local[1] = InView.Y;
    Local[2] = InView.Z;
    return Camera->ClassifySphere(Local, Radius) >= 3;
}
