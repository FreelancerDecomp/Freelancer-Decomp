// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_fuses.h"

#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "DALib.h"

#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char kPetalSource[] = "E:\\FL\\Scratch\\Source\\Common\\PetalDB.cpp";
    const char kWeaponModSource[] = "E:\\FL\\Scratch\\Source\\Common\\WeaponModDB.cpp";
    const unsigned kPetalNameChars = 0x400;
    const int kMaxPetalExtras = 0x10;
    // load_from_ini's GetFileSysPath destination is 0x400 bytes, not MAX_PATH: the frame slot
    // runs from ESP+0x15c4 (0631c247 LEA EAX,[ESP+0x15cc] with three args already pushed) up to
    // the SEH link at ESP+0x19c4 (0631c710 MOV ECX,[ESP+0x19b4] after the four register pops).
    const unsigned kPetalPathChars = 0x400;
    const float kDefaultShieldMod = 1.0f;
    // 0xbf800000: FuseAction's "no range" AtTEnd and Fuse's default lifetime.
    float const kMinusOne = -1.0f;
    // .rdata:063997d0 = 0x38000000 = 2^-15, the rand() scale in FuseAction::Randomize.
    float const kRandScale = 3.0517578125e-05f;

    PetalInterfaceDatabase GPetalDb;
    std::map<unsigned, FWeaponType> GWeaponTypes;

    // load_from_ini's value copies (0631c3d0 / 0631c3f0 / 0631c420) are unbounded inline strcpy
    // loops into 0x400-byte heap buffers; a longer value overruns the heap block in retail. The
    // port keeps the copy inside the buffer (retail UB, made deterministic). The source is never
    // null: INI_Reader::get_value_string(int) (06310a30) returns "" (.rdata:063997b0) for a
    // missing parameter and a buffer or string-table pointer otherwise, so there is no null arm.
    void CopyZ(char* Dest, unsigned DestChars, char const* Src)
    {
        unsigned Index = 0;
        for (; Index + 1 < DestChars && Src[Index] != '\0'; ++Index)
            Dest[Index] = Src[Index];
        Dest[Index] = '\0';
    }

    // 0631c30b-0631c346: DACOM stricmp against "room" (0), "cart" (5) and "prop" (6).
    int ObjectTableCategory(char const* ValueName)
    {
        if (CallDacomStricmp(ValueName, "room") == 0)
            return PetalCategoryRoom;
        if (CallDacomStricmp(ValueName, "cart") == 0)
            return PetalCategoryEquipmentCart;
        if (CallDacomStricmp(ValueName, "prop") == 0)
            return PetalCategoryProp;
        return -1;
    }
}

// ?m_FuseINIFiles@FuseDB@@1V?$list@U?$TString@$0BAE@@@...@std@@A (.data:063fc4c4; its
// dynamic initializer at .text:062be350 allocates the list head and registers the dtor).
std::list<TString<260>> FuseDB::m_FuseINIFiles;

void __cdecl FuseDB::AddFuseINIFile(char const* Path)
{
    // .text:062be260: TString<260> copy of Path (null -> empty; otherwise strlen clamped to
    // 0x103, copied, terminated) pushed onto the back of m_FuseINIFiles, size++. Nothing is
    // opened or parsed here: the consumer modules read these files themselves (freelancer.exe
    // FUN_0052a7a0, Server.dll FUN_06cfd4b0).
    TString<260> Stored;
    Stored = Path;
    m_FuseINIFiles.push_back(Stored);
}

void __cdecl FuseDB::Shutdown()
{
    // .text:062be310: unlinks and frees every path node, decrementing the size -- clear().
    m_FuseINIFiles.clear();
}

PetalInterfaceDatabase::PetalInterfaceDatabase()
{
}

PetalInterfaceDatabase::~PetalInterfaceDatabase()
{
    destroy();
}

void PetalInterfaceDatabase::destroy()
{
    unsigned Category = 0;
    for (; Category < PetalCategoryCount; ++Category)
        Categories[Category].clear();
}

int __cdecl PetalInterfaceDatabase::get_category(char const* Name)
{
    if (Name == nullptr || Name[0] == '\0')
        return -1;
    if (std::strcmp(Name, "Room") == 0)
        return PetalCategoryRoom;
    if (std::strcmp(Name, "Spaceship") == 0)
        return PetalCategorySpaceship;
    if (std::strcmp(Name, "Solar") == 0)
        return PetalCategorySolar;
    if (std::strcmp(Name, "Asteroid") == 0)
        return PetalCategoryAsteroid;
    if (std::strcmp(Name, "Equipment") == 0)
        return PetalCategoryEquipment;
    if (std::strcmp(Name, "Equipment Cart") == 0)
        return PetalCategoryEquipmentCart;
    if (std::strcmp(Name, "Prop") == 0)
        return PetalCategoryProp;

    FDUMP(0x100002, "%s(%d) : *** WARNING: Unknown Petal category name %s\n", kPetalSource, 0xbb,
          Name);
    return -1;
}

// 0631c870. The record: the nickname's CreateID (the text itself is dropped), the mesh as the
// record's own std::string (assign of strlen bytes), every extra name StringAlloc'd with the
// temporary flag (0631c943 PUSH 1). Then a map insert into Categories[Category] -- the first
// record under a nickname stays. Category is used as an index with no range test.
void PetalInterfaceDatabase::add_record(int Category, char const* Nickname, char const* Mesh,
                                        char** ExtraNames, int ExtraCount)
{
    petal_record Record;
    Record.NicknameId = CreateID(Nickname);
    Record.Mesh.assign(Mesh, strlen(Mesh));

    int ExtraIndex = 0;
    for (ExtraIndex = 0; ExtraIndex < ExtraCount; ++ExtraIndex)
        Record.Extras.push_back(StringAlloc(ExtraNames[ExtraIndex], true));

    Categories[Category].insert(
        std::pair<unsigned const, petal_record>(Record.NicknameId, Record));
}

// 0631cb40: lower_bound in Categories[Category] (no range test of Category), exact key ->
// the record at node+0x10, else 0.
PetalInterfaceDatabase::petal_record const* PetalInterfaceDatabase::find_record(
    int Category, unsigned long NicknameId) const
{
    std::map<unsigned, petal_record>::const_iterator It =
        Categories[Category].find(NicknameId);
    if (It == Categories[Category].end())
        return nullptr;
    return &It->second;
}

void PetalInterfaceDatabase::load_from_ini(char const* RelPath)
{
    // Common.dll.txt 0631c210-0631c744. Retail heap-allocates the 18 scratch buffers with
    // operator new(0x400) (0631c254/0631c269 plus the 16-iteration loop 0631c277-0631c28c) and
    // releases them at 0631c4b7-0631c4e1; we keep the same sizes on the stack and let scope
    // exit do the freeing.
    char FullPath[kPetalPathChars];
    // 0631c245: *DALib::Data is the IFileSystem.
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open petal database file %s\n", kPetalSource,
              0x86, FullPath);
        return;
    }

    while (Reader.read_header())
    {
        if (!Reader.is_header("ObjectTable"))
            continue;
        if (!Reader.read_value())
            continue;

        do
        {
            char const* ValueName = Reader.get_name_ptr();
            int Category = ObjectTableCategory(ValueName);
            if (Category < 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid category '%s' in petal database file %s\n",
                      kPetalSource, 0x59, ValueName, FullPath);
            }

            if (Reader.is_value_empty(0) || Reader.is_value_empty(1))
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: Invalid PetalDB entry %s in .ini file %s, cannot find nickname and mesh\n",
                      kPetalSource, 0x61, Reader.get_value_string(), FullPath);
            }

            char NicknameBuf[kPetalNameChars];
            char MeshBuf[kPetalNameChars];
            char ExtraBufs[kMaxPetalExtras][kPetalNameChars];
            char* ExtraPtrs[kMaxPetalExtras];

            // 0631c3bf-0631c3f8: two inline strcpy loops, value 0 into the nickname buffer and
            // value 1 into the mesh buffer.
            CopyZ(NicknameBuf, kPetalNameChars, Reader.get_value_string(0));
            CopyZ(MeshBuf, kPetalNameChars, Reader.get_value_string(1));

            // 0631c3fa-0631c438: EDI starts at 2 (the value index), ESI at 0 (the buffer index),
            // and the loop runs while is_value_empty(EDI) is false. Retail has no upper bound on
            // ESI even though only 16 buffers exist, so a line with more than 16 extra values
            // reads garbage pointers past the array and writes through them (retail UB); the
            // port stops at 16 instead, deterministically.
            int ExtraCount = 0;
            unsigned ParamIndex = 2;
            while (!Reader.is_value_empty(ParamIndex) && ExtraCount < kMaxPetalExtras)
            {
                CopyZ(ExtraBufs[ExtraCount], kPetalNameChars, Reader.get_value_string(ParamIndex));
                ExtraPtrs[ExtraCount] = ExtraBufs[ExtraCount];
                ExtraCount += 1;
                ParamIndex += 1;
            }

            // 0631c43a CMP EBX,-1 / 0631c441 JZ -- an unrecognised value name still parses its
            // strings, it just never reaches add_record.
            if (Category != -1)
                add_record(Category, NicknameBuf, MeshBuf, ExtraPtrs, ExtraCount);
        } while (Reader.read_value());
    }

    // 0631c481-0631c485: retail closes the reader explicitly once read_header runs dry. The
    // failed-open path at 0631c490 does not.
    Reader.close();
}

void __cdecl PetalDB_load(char const* RelPath)
{
    GPetalDb.load_from_ini(RelPath);
}

void __cdecl PetalDB_destroy()
{
    GPetalDb.destroy();
}

PetalInterfaceDatabase const* __cdecl PetalDB_get()
{
    return &GPetalDb;
}

// FUN_06341790 (called from DataConfig::startup 063093bb with the resolved path; its 0 result
// is ignored). The path is opened as given. Each [WeaponType] starts with no current type;
// "nickname" inserts {id, empty map} (an existing entry is kept) and makes it current;
// "shield_mod" with a current type inserts CreateID(value 0) -> value 1 (first one stays).
void __cdecl WeaponModDB_load(char const* Path)
{
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to open WeaponMod file %s.", kWeaponModSource,
              0x48, Path);
        return;
    }

    FWeaponType* Current = nullptr;
    while (Reader.read_header())
    {
        if (!Reader.is_header("WeaponType"))
            continue;
        Current = nullptr;
        if (!Reader.read_value())
            continue;

        do
        {
            if (Reader.is_value("nickname"))
            {
                unsigned NicknameId = CreateID(Reader.get_value_string());
                std::map<unsigned, FWeaponType>::iterator It = GWeaponTypes.find(NicknameId);
                if (It == GWeaponTypes.end())
                {
                    FWeaponType Type;
                    Type.NicknameId = NicknameId;
                    It = GWeaponTypes.insert(std::pair<unsigned, FWeaponType>(NicknameId, Type))
                             .first;
                }
                Current = &It->second;
            }
            else if (Reader.is_value("shield_mod") && Current != nullptr)
            {
                unsigned ShieldId = CreateID(Reader.get_value_string(0));
                float Modifier = Reader.get_value_float(1);
                if (Current->ShieldMods.find(ShieldId) == Current->ShieldMods.end())
                    Current->ShieldMods[ShieldId] = Modifier;
            }
        } while (Reader.read_value());
    }
}

void __cdecl WeaponModDB_destroy()
{
    GWeaponTypes.clear();
}

float __cdecl WeaponModDB_GetShieldMod(unsigned WeaponTypeId, unsigned ShieldTypeId)
{
    std::map<unsigned, FWeaponType>::const_iterator WeaponIt = GWeaponTypes.find(WeaponTypeId);
    if (WeaponIt == GWeaponTypes.end())
        return kDefaultShieldMod;

    std::map<unsigned, float>::const_iterator ModIt = WeaponIt->second.ShieldMods.find(ShieldTypeId);
    if (ModIt == WeaponIt->second.ShieldMods.end())
        return kDefaultShieldMod;
    return ModIt->second;
}

// ---------------------------------------------------------------------------
// FuseAction -- Common.dll .text:062bd3d0-062bd790 / 062bee90-062bef64.
// ---------------------------------------------------------------------------

FuseAction::FuseAction()
    : AtT(kMinusOne)
    , AtTEnd(kMinusOne)
    , RefCount(0)
    , bTriggered(false)
{
    // .text:062bd3d0: +0x04 = +0x08 = 0xbf800000, +0x0c = 0, +0x10 = 0, empty vector at +0x14.
}

FuseAction::FuseAction(FuseAction const& Other)
    : AtT(Other.AtT)
    , AtTEnd(Other.AtTEnd)
    , RefCount(Other.RefCount)
    , bTriggered(Other.bTriggered)
    , TriggeredSubObjs(Other.TriggeredSubObjs)
{
    // .text:062bd460: copies +0x04/+0x08/+0x0c/+0x10, then allocates exactly size() ids and
    // copies the sub-object vector.
}

FuseAction::~FuseAction()
{
    // .text:062bd400: frees the sub-object vector storage.
}

FuseAction& FuseAction::operator=(FuseAction const& Other)
{
    // .text:062bd4e0: scalars unconditionally, the vector only when it is a different object
    // (the three-way VC6 vector::operator= inlined).
    AtT = Other.AtT;
    AtTEnd = Other.AtTEnd;
    RefCount = Other.RefCount;
    bTriggered = Other.bTriggered;
    if (&TriggeredSubObjs != &Other.TriggeredSubObjs)
        TriggeredSubObjs = Other.TriggeredSubObjs;
    return *this;
}

int FuseAction::Load()
{
    // .text:062bee90: ++[this+0xc], returns the new count.
    RefCount = RefCount + 1;
    return RefCount;
}

int FuseAction::UnLoad()
{
    // .text:062beea0: --[this+0xc], returns the new count.
    RefCount = RefCount - 1;
    return RefCount;
}

bool FuseAction::IsTriggered() const
{
    // .text:06286e60 (ICF-shared with CEquip::IsActive): `MOV AL,[ECX+0x10]; RET`.
    return bTriggered;
}

bool FuseAction::IsTriggered(unsigned short SubObjId) const
{
    // .text:062bd6f0: false while the flag is clear; otherwise a linear find of SubObjId.
    if (!bTriggered)
        return false;
    std::vector<unsigned short>::const_iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    return It != TriggeredSubObjs.end();
}

void FuseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // .text:062bd730: set the flag, then append SubObjId unless it is already present. The
    // source id is not used by the base class. The second find retail runs after the insert
    // (062bd768-062bd781) has no side effect.
    (void)SourceId;
    bTriggered = true;
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It == TriggeredSubObjs.end())
        TriggeredSubObjs.insert(TriggeredSubObjs.end(), 1, SubObjId);
}

void FuseAction::UnTrigger(unsigned short SubObjId)
{
    // .text:062bd790: find SubObjId and erase it (shift the tail down, _Last -= 2). The flag at
    // +0x10 is left alone.
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It != TriggeredSubObjs.end())
        TriggeredSubObjs.erase(It);
}

bool FuseAction::ShouldRandomize() const
{
    // .text:062bd430: `CMP dword [ECX+8],0xbf800000; JZ -> 0`.
    return AtTEnd != kMinusOne;
}

float FuseAction::Randomize()
{
    // .text:062bef20: Begin = AtT, End = AtTEnd, r = rand(); AtTEnd = -1;
    // AtT = (r * 2^-15) * (End - Begin) + Begin (FILD; FMUL DAT_063997d0; FLD End; FSUB Begin;
    // FMULP; FADD Begin; FST [ESI+4]).
    float const Begin = AtT;
    float const End = AtTEnd;
    int const Roll = rand();
    AtTEnd = kMinusOne;
    double const Scaled = (double)Roll * kRandScale;
    double const Span = (double)End - Begin;
    AtT = (float)(Scaled * Span + Begin);
    return AtT;
}

float FuseAction::GetTriggerTime() const
{
    // .text:062bd450: `FLD [ECX+4]`.
    return AtT;
}

void FuseAction::CopyArchProperties(FuseAction const& Other)
{
    // .text:062beeb0: copies the archetype part only -- +0x04 AtT and +0x08 AtTEnd.
    AtT = Other.AtT;
    AtTEnd = Other.AtTEnd;
}

bool FuseAction::ReadFuseActionValue(INI_Reader& Reader)
{
    // .text:062beed0: consumes only "at_t" (.rdata:0639ef5c). Value 0 is the trigger time; a
    // non-empty value 1 makes it a random range [value0, value1]. Anything else -> false so the
    // subclass handles it.
    if (!Reader.is_value("at_t"))
        return false;
    AtT = Reader.get_value_float(0);
    if (!Reader.is_value_empty(1))
        AtTEnd = Reader.get_value_float(1);
    return true;
}

// ---------------------------------------------------------------------------
// Fuse -- Common.dll .text:062bd020-062be255.
// ---------------------------------------------------------------------------

Fuse::Fuse(ID_String const& NicknameIn, float LifetimeIn)
    : Nickname(NicknameIn)
    , LifetimeSec(LifetimeIn)
    , bDeathFuse(false)
    , bNeedsRandomize(false)
{
    // .text:062bd020: +0x04 nickname, +0x08 lifetime, +0x0c = 0, empty multimap (_Multi = 1),
    // +0x24 = 0, empty burn list.
}

Fuse::Fuse(Fuse const& Other)
    : Nickname(Other.Nickname)
    , LifetimeSec(Other.LifetimeSec)
    , bDeathFuse(Other.bDeathFuse)
    , bNeedsRandomize(Other.bNeedsRandomize)
{
    // .text:062bd960: nickname/lifetime/death flag and +0x24 are copied; the burn list starts
    // empty (062bd9b6-062bd9c6) -- ignitions are never copied. Retail then runs an inlined
    // FreeActions + clear over its own freshly made (empty) tree (062bd9f7-062bda32), which
    // does nothing. Finally every action of Other is cloned through vftable slot 1
    // (062bda55 CALL [EAX+0x4], no null check) and inserted under Other's key (the node key at
    // +0xc, not the clone's trigger time) with the multimap insert walk (062bda78-062bdb4b).
    for (std::multimap<float, FuseAction*>::const_iterator It = Other.Actions.begin();
         It != Other.Actions.end(); ++It)
    {
        FuseAction* Clone = It->second->Slot04();
        Actions.insert(std::pair<float const, FuseAction*>(It->first, Clone));
    }
}

Fuse::~Fuse()
{
    // .text:062bd7e0: deletes every action through its virtual dtor (null-checked,
    // 062bd833), erases the tree, clears the burn list; the containers free their heads.
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end();
         ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Actions.clear();
    Burns.clear();
}

Fuse& Fuse::operator=(Fuse const& Other)
{
    // .text:062bd100: the compiler's memberwise assignment. The action tree is copied node by
    // node (FUN_062be950) -- the FuseAction pointers are shared, not cloned -- when it is a
    // different tree; +0x24 is copied; the burn list is assigned element-wise (062bd1e1-
    // 062bd262) when it is a different list.
    Nickname = Other.Nickname;
    LifetimeSec = Other.LifetimeSec;
    bDeathFuse = Other.bDeathFuse;
    if (&Actions != &Other.Actions)
        Actions = Other.Actions;
    bNeedsRandomize = Other.bNeedsRandomize;
    if (&Burns != &Other.Burns)
        Burns = Other.Burns;
    return *this;
}

void Fuse::FreeActions()
{
    // .text:062bdb80: delete every action (null-checked), then empty the tree.
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end();
         ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Actions.clear();
}

bool Fuse::ReadFuseValues(INI_Reader& Reader)
{
    // .text:062bdc50: FreeActions through the vftable (062bdc57 CALL [EAX+0x4]), reset the
    // nickname, lifetime = -1 and the death flag, then read the [fuse] section's values:
    // name (.rdata:06399f10) -> CreateID(get_value_string()), lifetime -> value 0,
    // death_fuse -> bool value 0. Always true. +0x24 and the burn list are not touched.
    FreeActions();
    Nickname.clear();
    LifetimeSec = kMinusOne;
    bDeathFuse = false;
    while (Reader.read_value())
    {
        if (Reader.is_value("name"))
            Nickname.Id = CreateID(Reader.get_value_string());
        else if (Reader.is_value("lifetime"))
            LifetimeSec = Reader.get_value_float(0);
        else if (Reader.is_value("death_fuse"))
            bDeathFuse = Reader.get_value_bool(0);
    }
    return true;
}

bool Fuse::IsBurning() const
{
    // .text:062bd0f0: burn list size != 0.
    return !Burns.empty();
}

bool Fuse::IsSpent() const
{
    // .text:062bdcf0: true for the first burn whose window is fully open.
    // `FLD Lower; FCOMP 0.0; TEST AH,0x41; JP next` keeps Lower <= 0 (NaN skips), then
    // `FLD Upper; FCOMP 1.0; TEST AH,1; JZ found` needs Upper >= 1 (NaN fails).
    for (std::list<FFuseBurn>::const_iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->Lower <= 0.0f && It->Upper >= 1.0f)
            return true;
    }
    return false;
}

bool Fuse::IsSpentAt(float T) const
{
    // .text:062bdd30: `FCOMP 0.0; TEST AH,1; JNZ` -> false when T < 0 or NaN; `FCOMP 1.0;
    // TEST AH,0x41; JP` -> false when T > 1. Then true for the first burn whose window holds T:
    // `FLD Lower; FCOMP T; TEST 0x41; JP next` (Lower <= T) and `FLD T; FCOMP Upper; TEST 0x41;
    // JNP found` (T <= Upper).
    if (!(T >= 0.0f))
        return false;
    if (!(T <= 1.0f))
        return false;
    for (std::list<FFuseBurn>::const_iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->Lower <= T && T <= It->Upper)
            return true;
    }
    return false;
}

bool Fuse::IgniteAt(unsigned SourceId, unsigned short SubObjId, float AtTIn)
{
    // .text:062bdd90: an existing burn with the same sub-object and ignition time
    // (`FCOMP; TEST AH,0x44; JNP` = equal) refuses; otherwise push_back {SourceId, SubObjId,
    // AtT, AtT, AtT} and return true.
    for (std::list<FFuseBurn>::const_iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->SubObjId == SubObjId && It->IgniteT == AtTIn)
            return false;
    }
    FFuseBurn NewBurn;
    NewBurn.SourceId = SourceId;
    NewBurn.SubObjId = SubObjId;
    NewBurn.IgniteT = AtTIn;
    NewBurn.Lower = AtTIn;
    NewBurn.Upper = AtTIn;
    Burns.push_back(NewBurn);
    return true;
}

void Fuse::Burn(float Dt)
{
    // .text:062bde40. Nothing while no burn is live. Step = Dt / Lifetime (FDIV, stored back as
    // a float). First pass widens every window: Lower = Lower - Step unless that is below 0
    // (`FLD 0.0; FCOMP; TEST AH,0x41; JNZ keep` -> replaced by 0 only when 0 > x), Upper =
    // Step + Upper unless above 1 (`TEST AH,5; JP keep` -> replaced by 1 only when 1 < y).
    // Second pass: for every burn, the actions keyed in [lower_bound(Lower),
    // upper_bound(Upper)) (FUN_062becd0 / FUN_062bed30) that are not yet triggered for the
    // burn's sub-object (vftable +0x14) are triggered (vftable +0x1c) with the burn's source.
    if (Burns.empty())
        return;
    float const Step = (float)((double)Dt / LifetimeSec);

    for (std::list<FFuseBurn>::iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        double const LowerX = (double)It->Lower - Step;
        It->Lower = (LowerX < 0.0) ? 0.0f : (float)LowerX;
        double const UpperX = (double)Step + It->Upper;
        It->Upper = (UpperX > 1.0) ? 1.0f : (float)UpperX;
    }

    for (std::list<FFuseBurn>::iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        std::multimap<float, FuseAction*>::iterator First = Actions.lower_bound(It->Lower);
        std::multimap<float, FuseAction*>::iterator Last = Actions.upper_bound(It->Upper);
        for (std::multimap<float, FuseAction*>::iterator ActionIt = First; ActionIt != Last;
             ++ActionIt)
        {
            FuseAction* Action = ActionIt->second;
            if (!Action->IsTriggered(It->SubObjId))
                Action->Trigger(It->SourceId, It->SubObjId);
        }
    }
}

bool Fuse::UnBurn(unsigned short SubObjId, float AtTIn)
{
    // .text:062bdf50: find the burn with this sub-object and ignition time (equal compare); if
    // none, false. Otherwise every action keyed in [lower_bound(Lower), upper_bound(Upper))
    // that reports itself triggered at all (vftable +0x18, the no-argument IsTriggered) is
    // untriggered for SubObjId (vftable +0x20); then the burn node is unlinked and freed and
    // the count decremented. True.
    for (std::list<FFuseBurn>::iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->SubObjId != SubObjId || !(It->IgniteT == AtTIn))
            continue;

        std::multimap<float, FuseAction*>::iterator First = Actions.lower_bound(It->Lower);
        std::multimap<float, FuseAction*>::iterator Last = Actions.upper_bound(It->Upper);
        for (std::multimap<float, FuseAction*>::iterator ActionIt = First; ActionIt != Last;
             ++ActionIt)
        {
            FuseAction* Action = ActionIt->second;
            if (Action->IsTriggered())
                Action->UnTrigger(It->SubObjId);
        }

        Burns.erase(It);
        return true;
    }
    return false;
}

void Fuse::Load() const
{
    // .text:062be040: every action's Load (vftable +0xc), no null check.
    for (std::multimap<float, FuseAction*>::const_iterator It = Actions.begin();
         It != Actions.end(); ++It)
        It->second->Load();
}

void Fuse::UnLoad() const
{
    // .text:062be070: every action's UnLoad (vftable +0x10), no null check.
    for (std::multimap<float, FuseAction*>::const_iterator It = Actions.begin();
         It != Actions.end(); ++It)
        It->second->UnLoad();
}

void Fuse::RandomizeActions()
{
    // .text:062be0a0: only when +0x24 is set. Walks the tree; an action whose ShouldRandomize
    // (vftable +0x24) is true is erased from its node (FUN_062be400 returns the successor, the
    // walk continues from there), Randomize'd (vftable +0x28, result discarded), and inserted
    // again under its new trigger time ([ESI+4]) with the multimap insert walk. The flag is
    // cleared afterwards.
    if (!bNeedsRandomize)
        return;

    std::multimap<float, FuseAction*>::iterator It = Actions.begin();
    while (It != Actions.end())
    {
        FuseAction* Action = It->second;
        if (Action->ShouldRandomize())
        {
            It = Actions.erase(It);
            Action->Randomize();
            Actions.insert(std::pair<float const, FuseAction*>(Action->GetTriggerTime(), Action));
        }
        else
        {
            ++It;
        }
    }
    bNeedsRandomize = false;
}

bool Fuse::OverrideLifetime(float NewLifetime)
{
    // .text:062be240: only while no burn is live.
    if (!Burns.empty())
        return false;
    LifetimeSec = NewLifetime;
    return true;
}

// ===========================================================================
// Explicit STL instantiation exported like retail's. Retail Common.dll exports all 47
// members of its VC6 std::vector<unsigned short>, the type of FuseAction's
// sub-object id list (copy ctor 0x5d460, operator= 0x5d4e0). The element is a real
// unsigned short, not a VC6 wide character. Importing declaration: common_fuses.h.
// ===========================================================================
template class COMMON_API std::vector<unsigned short>;
