// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "engine_character.h"

#include "character_info.h" // GetCharacterInfoDb (FUN_004356c0), AcquireScriptByName (FUN_00434e90)
#include "common_goods.h"
#include "common_ini.h"
#include "misc_util.h"

#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "readfile.h" // IFileSystem : IComponentFactory (DALib::Data into FDeformLodDesc)

#include <stdint.h>
#include <string.h>

static_assert(sizeof(FEngineCharacterPartPath) == 0x94, "EngineCharacter part path is 0x94");
static_assert(sizeof(FDeformPartDesc) == 0x18, "EngineCharacter part entry is 0x18");
static_assert(sizeof(FDeformLodDesc) == 0xc, "EngineCharacter part source is 0xc");
static_assert(offsetof(FEngineCharacterBodyDesc, Entry) == 0x68, "BodyDesc entries at +0x68");
static_assert(offsetof(FEngineCharacterBodyDesc, Part) == 0x128, "BodyDesc paths at +0x128");
static_assert(sizeof(FEngineCharacterBodyDesc) == 0x5c8, "BodyDesc is 0x5c8");
static_assert(sizeof(FEngineCharacterBody) == 0x298, "EngineCharacter body is 0x298");
static_assert(offsetof(FEngineCharacterBody, AnimNames) == 0x44, "body anim names at +0x44");
static_assert(offsetof(FEngineCharacterBody, AnimIds) == 0x254, "body anim ids at +0x254");
static_assert(offsetof(FEngineCharacterBody, RootTransform) == 0x268, "body transform at +0x268");
static_assert(offsetof(FEngineCharacterAccessoryLink, AccessoryHardpoint) == 0x84, "link +0x84");
static_assert(offsetof(FEngineCharacterAccessoryLink, CharacterHardpoint) == 0xc8, "link +0xc8");
static_assert(offsetof(FEngineCharacterAccessoryLink, MaterialPaths) == 0x10c, "link +0x10c");

namespace
{
    char const kEngineCharacterH[] = "cmn\\EngineCharacter.h"; // .rdata 0x005d7188

    // The MSVCP60 empty string (.rdata 0x005c7078) every "c_str or empty" read falls back to.
    char const kEmpty[] = "";

    // std::string::c_str() of a VC6 string reads its +4 pointer and falls back to the shared
    // empty string when it is null (0x004c9c63..0x004c9c75, 0x004c9f6a..0x004c9f77).
    char const* CStrOrEmpty(std::string const& Text)
    {
        return Text.c_str();
    }

    void EmptyName(TString<0x80>& Name)
    {
        Name.Length = 0;
        Name.Data[0] = '\0';
    }

    // IDeformable::Play takes every trailing argument as a dword; a float goes through as its
    // bit pattern (the 0x3f800000 / 0xbf800000 immediates at 0x00560b91 / 0x00560b9f).
    void* DwordArg(float Value)
    {
        uintptr_t Bits = 0;
        memcpy(&Bits, &Value, sizeof(Value));
        return reinterpret_cast<void*>(Bits);
    }

    void* DwordArg(unsigned Value)
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(Value));
    }
}

// ============================================================================================
// FEngineCharacterBodyDesc
// ============================================================================================

FEngineCharacterBodyDesc::FEngineCharacterBodyDesc(Costume const& Source)
{
    // FUN_004c9f00. 0x004c9f05..0x004c9f42: the entries and the paths.
    for (int Index = 0; Index < 8; ++Index)
    {
        Entry[Index].LodCount = 0;
        Entry[Index].Lods = nullptr;
        Entry[Index].Field08 = 0;
        Entry[Index].Path = nullptr;
        Entry[Index].Flags = 0;
        Entry[Index].LodLimit = 0xFFFFFFFFu;
    }
    for (int Index = 0; Index < 8; ++Index)
    {
        Part[Index].Path.Length = 0;
        Part[Index].Path.Data[0] = '\0';
    }
    Count = 0;
    Entries = &Entry[0];

    // 0x004c9f4d..0x004c9f97: PartIds[1] as type 0, in front of whatever is there.
    unsigned long const Id1 = Source.PartIds[1];
    CostumeDescriptions const* Descs = GetCostumeDescriptions();
    if (Id1 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(0, Id1);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        int const Old = Count;
        if (Old < 8)
        {
            Count = Old + 1;
            int Slot = Old;
            if (Old != 0)
            {
                ShiftPartsUp();
                Slot = 0;
            }
            SetPart(Slot, 0, Path);
        }
    }

    // 0x004c9f9c..0x004c9fde: PartIds[0] as type 1, appended.
    unsigned long const Id0 = Source.PartIds[0];
    Descs = GetCostumeDescriptions();
    if (Id0 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(1, Id0);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        int const Old = Count;
        if (Old < 8)
        {
            Count = Old + 1;
            SetPart(Old, 1, Path);
        }
    }

    // 0x004c9fe3..0x004ca019: PartIds[2] as type 2 through AddPart.
    unsigned long const Id2 = Source.PartIds[2];
    Descs = GetCostumeDescriptions();
    if (Id2 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(2, Id2);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        AddPart(2, Path);
    }

    // 0x004ca01e..0x004ca061: PartIds[3] as type 3, appended.
    unsigned long const Id3 = Source.PartIds[3];
    Descs = GetCostumeDescriptions();
    if (Id3 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(3, Id3);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        int const Old = Count;
        if (Old < 8)
        {
            Count = Old + 1;
            SetPart(Old, 3, Path);
        }
    }
}

void FEngineCharacterBodyDesc::AddPart(int TypeId, char const* Path)
{
    // FUN_004ca070.
    int Slot = Count;
    if (Slot >= 8)
        return;
    Count = Slot + 1;
    if (TypeId == 0 && Slot != 0)
    {
        // 0x004ca0a1..0x004ca14b: the FUN_004ca2f0 body inlined.
        ShiftPartsUp();
        Slot = 0;
    }
    // 0x004ca157..0x004ca205: the FUN_004ca220 body inlined.
    SetPart(Slot, TypeId, Path);
}

void FEngineCharacterBodyDesc::SetPart(int Index, int TypeId, char const* Path)
{
    // FUN_004ca220.
    FEngineCharacterPartPath& Record = Part[Index];
    Record.Path = Path; // null -> "" (0x004ca23c), else <= 0x7f chars
    Record.Source.FileSystem = DALib::Data;
    Record.Source.Name = Record.Path.Data;
    FDeformPartDesc& Slot = Entry[Index];
    Slot.Lods = &Record.Source;
    Slot.Field08 = 0;
    Slot.Path = nullptr;
    Slot.LodCount = 1;
    Slot.Flags = 1;
    Record.TypeId = TypeId;
}

void FEngineCharacterBodyDesc::ShiftPartsUp()
{
    // FUN_004ca2f0: from slot 7 down to slot 1, each takes slot - 1.
    for (int Index = 7; Index >= 1; --Index)
    {
        FEngineCharacterPartPath& To = Part[Index];
        FEngineCharacterPartPath const& From = Part[Index - 1];
        To.Path = From.Path.Data;                    // strlen, <= 0x7f
        To.TypeId = From.TypeId;                     // +0x90
        To.Source = From.Source;                     // +0x84..+0x8c
        Entry[Index] = Entry[Index - 1];             // six dwords
        Entry[Index].Lods = &To.Source;
        To.Source.Name = To.Path.Data;
    }
}

void* FEngineCharacterBodyDesc::CreateDeformable()
{
    // FUN_004ca3c0.
    void* Instance = nullptr;
    if (Count == 0)
        return nullptr;

    Archetype::FCliMaterialEntry* Materials[8];
    for (int Index = 0; Index < Count; ++Index)
    {
        // 0x004ca3e0..0x004ca3f7: Entries[i]+4 -> the LOD descriptor, +4 -> the path.
        char const* const PathText = Entries[Index].Lods->Name;
        Materials[Index] = Archetype::ResolveMaterialEntry(PathText, true, 0xffffffffu);
    }

    // 0x004ca405..0x004ca41b: IDeformable slot 5 with this desc.
    if (DALib::Deform->CreateAlias(&Instance, reinterpret_cast<int*>(this), nullptr, nullptr,
                                   nullptr) != 0)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: BodyDesc - failed to create deformable with %d parts\n",
              kEngineCharacterH, 0xd5, Count);
        Instance = nullptr;
    }

    for (int Index = 0; Index < Count; ++Index)
        Archetype::ReleaseMaterialEntrySlot(&Materials[Index]); // FUN_0041b030
    return Instance;
}

// ============================================================================================
// FEngineCharacterAccessoryLink
// ============================================================================================

FEngineCharacterAccessoryLink::FEngineCharacterAccessoryLink()
{
    // FUN_004c9de0.
    Mesh.Length = 0;
    Mesh.Data[0] = '\0';
    AccessoryHardpoint.Length = 0;
    AccessoryHardpoint.Data[0] = '\0';
    CharacterHardpoint.Length = 0;
    CharacterHardpoint.Data[0] = '\0';
    MaterialPaths.Cookie = 0;
    MaterialPaths.Begin = nullptr;
    MaterialPaths.End = nullptr;
    MaterialPaths.Capacity = nullptr;
}

FEngineCharacterAccessoryLink::~FEngineCharacterAccessoryLink()
{
    // FUN_004de730 / FUN_004ca610: free the buffer, zero the three pointers.
    delete[] MaterialPaths.Begin;
    MaterialPaths.Begin = nullptr;
    MaterialPaths.End = nullptr;
    MaterialPaths.Capacity = nullptr;
}

void* FEngineCharacterAccessoryLink::Connect(long Bone)
{
    // FUN_004c9e20.
    void* const Instance = MiscUtilLoadEngineInstance(Mesh.Data, DALib::Data, nullptr, nullptr,
                                                      &MaterialPaths, nullptr, nullptr);
    long Hardpoint = -1;
    bool bFailed = true;
    if (FindHardpoint(Bone, CharacterHardpoint.Data, Hardpoint))
    {
        // 0x004c9e75..0x004c9ebd: the two keys, CRC'd, then IHardpoint slot 6.
        FHardpointKey AccessoryKey;
        AccessoryKey.Name = AccessoryHardpoint.Data;
        AccessoryKey.Crc = DACOM_CRC::GetCRC32(AccessoryHardpoint.Data);
        FHardpointKey CharacterKey;
        CharacterKey.Name = CharacterHardpoint.Data;
        CharacterKey.Crc = DACOM_CRC::GetCRC32(CharacterHardpoint.Data);
        bFailed = DALib::Hardpoint->ConnectHardpoints(reinterpret_cast<void*>(Hardpoint),
                                                     &CharacterKey, Instance,
                                                     &AccessoryKey) != 0;
    }
    if (bFailed)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Accessory - connection failed! (%s)  acc_hp='%s'  "
              "obj_hp='%s'\n",
              kEngineCharacterH, 0x4e, Mesh.Data, AccessoryHardpoint.Data,
              CharacterHardpoint.Data);
    }
    return Instance;
}

// ============================================================================================
// FEngineCharacterBody
// ============================================================================================

FEngineCharacterBody::FEngineCharacterBody()
{
    // 0x004c9b46..0x004c9bc9. The holders are built by their member initialisers (FUN_00420d40:
    // instance -1, both material slots null).
    for (int Index = 0; Index < 4; ++Index)
        EmptyName(AnimNames[Index]);
    // FUN_004215b0 on +0x268, then +0x28c..+0x294 zeroed.
    memset(&RootTransform, 0, sizeof(RootTransform));
    RootTransform.Rotation.D[0][0] = 1.0f;
    RootTransform.Rotation.D[1][1] = 1.0f;
    RootTransform.Rotation.D[2][2] = 1.0f;
    Deformable = nullptr;
    for (int Index = 0; Index < 4; ++Index)
    {
        EmptyName(AnimNames[Index]);
        AnimIds[Index] = -1;
    }
    AnimRate = 1.0f;
}

FEngineCharacterBody::~FEngineCharacterBody()
{
    // FUN_004ca530.
    Reset();
}

void FEngineCharacterBody::StopAnimations()
{
    // FUN_004c9a30.
    if (Deformable == nullptr)
        return;
    DALib::Deform->DestroyAll(Deformable);
    for (int Index = 0; Index < 4; ++Index)
    {
        if (AnimIds[Index] != -1)
        {
            EmptyName(AnimNames[Index]);
            AnimIds[Index] = -1;
        }
    }
}

void FEngineCharacterBody::Reset()
{
    // FUN_004ca4b0.
    StopAnimations();
    for (int Index = 0; Index < 4; ++Index)
        ClientMeshHolderRelease(&Accessories[Index]); // FUN_00420f70
    if (Deformable != nullptr)
    {
        DALib::Deform->Destroy(Deformable);
        Deformable = nullptr;
    }
}

long FEngineCharacterBody::GetRootBone() const
{
    // FUN_004ca480.
    int Bone = -1;
    if (Deformable != nullptr)
        DALib::Deform->GetFirstBone(Deformable, &Bone);
    return Bone;
}

FTransform* FEngineCharacterBody::GetRootTransform(FTransform* Out) const
{
    // FUN_004cba10. The bone lookup is inlined (the same body as FUN_004ca480).
    int Bone = -1;
    if (Deformable != nullptr)
        DALib::Deform->GetFirstBone(Deformable, &Bone);
    float const* const Source = DALib::Engine->GetInstanceTransform(reinterpret_cast<void*>(Bone));
    memcpy(Out, Source, sizeof(FTransform));
    return Out;
}

void FEngineCharacterBody::Advance(float Dt)
{
    // FUN_004cba60.
    if (Deformable == nullptr)
        return;
    float const Scaled = Dt * AnimRate;

    int Bone = -1;
    DALib::Deform->GetFirstBone(Deformable, &Bone);
    DALib::Anim->MixerWalk(static_cast<unsigned long>(Bone), Scaled);

    // 0x004cbaaf..0x004cbadf: the bone read again (the same test on +0 as FUN_004ca480), then
    // IEngine slot 27 with the scaled time as its third argument.
    int Root = -1;
    if (Deformable != nullptr)
        DALib::Deform->GetFirstBone(Deformable, &Root);
    void* TimeArg = nullptr;
    memcpy(&TimeArg, &Scaled, sizeof(Scaled));
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Root), 0, TimeArg);

    // 0x004cbae2..0x004cbb30: IDeformable slot 26 into a zeroed int; none left -> every slot
    // emptied (no id test here, unlike StopAnimations).
    int Running = 0;
    DALib::Deform->GetListCount(Deformable, &Running);
    if (Running == 0)
    {
        for (int Index = 0; Index < 4; ++Index)
        {
            EmptyName(AnimNames[Index]);
            AnimIds[Index] = -1;
        }
    }
}

bool FEngineCharacterBody::Build(Costume const& Key)
{
    // FUN_0059b220 (thiscall on UIWinDeform +0x8c, RET 4) -- UIWin.cpp's own copy of the body
    // build FindOrCreate (FUN_004c9a80) inlines. 0x0059b245..0x0059b276: FUN_004ca4b0's body.
    Reset();

    // 0x0059b279..0x0059b3e6: the part description (the eh-vector ctors 0x0059b690 /
    // 0x0059b890, then FUN_0059b6b0 per bodypart: (0, PartIds[1]), (1, PartIds[0]),
    // (2, PartIds[2]), (3, PartIds[3]) -- FUN_004c9f00's order) and the deformable
    // (FUN_004ca3c0's body: a material reference per part, FUN_0041aa50(path, 1, -1) at
    // 0x0059b339, IDeformable slot 5, FDUMP line 0xd5 on failure, the references released).
    {
        FEngineCharacterBodyDesc Desc(Key);
        Deformable = Desc.CreateDeformable();
    }

    // 0x0059b3e8..0x0059b638: the accessories, as FindOrCreate's loop (`CMP i,4 / JA`, the
    // holder pointer advanced for every index); FUN_004c9e20's body is inlined here.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    FMeshHolder* Holder = &Accessories[0];
    for (int Index = 0; Index < static_cast<int>(Key.AccessoryCount); ++Index, ++Holder)
    {
        if (static_cast<unsigned>(Index) > 4u)
            break;
        accessory const* const Found = Descs->find_accessory(Key.AccessoryIds[Index]);
        if (Found == nullptr)
            continue;

        FEngineCharacterAccessoryLink Link; // FUN_005a6900 + the three TString clears
        Link.Mesh = CStrOrEmpty(Found->Mesh);                              // FUN_004cec20
        Link.CharacterHardpoint = CStrOrEmpty(Found->CharacterHardpoint);  // FUN_004cebc0
        Link.AccessoryHardpoint = CStrOrEmpty(Found->AccessoryHardpoint);  // FUN_004cebc0
        // 0x0059b4b6..0x0059b4d9: the root bone (-1 without a deformable, IDeformable slot 18).
        Holder->EngineInstance = Link.Connect(GetRootBone());

        // 0x0059b5aa..0x0059b5f1: with a material library, the first one (CacheString::clear,
        // StringAlloc(name, 1)) into the holder (FUN_00420f30).
        if (!Found->Materials.empty())
        {
            char const* const First = CStrOrEmpty(Found->Materials.front());
            CacheString Material;
            Material.clear();
            Material.Value = StringAlloc(First, true);
            ClientMeshHolderSetMaterials(Holder, Material.Value, nullptr);
        }
    } // Link destroyed (FUN_004de730)

    // 0x0059b63e..0x0059b652: SETNZ on the deformable.
    return Deformable != nullptr;
}

bool FEngineCharacterBody::IsPlaybackIdle() const
{
    // FUN_00560a80: IDeformable slot 26 on +0x00 (no null test) into a zeroed int, SETZ.
    int Running = 0;
    DALib::Deform->GetListCount(Deformable, &Running);
    return Running == 0;
}

bool FEngineCharacterBody::PlayAnimation(char const* Name, unsigned Arg2, float Arg3, float Arg4,
                                         int Arg5)
{
    // FUN_00560ab0. 0x00560ab3..0x00560afa: the root bone (-1 without a deformable, slot 18),
    // IEngine slot 40 on it, the 12 dwords into +0x268 -- FUN_004cba10's body.
    GetRootTransform(&RootTransform);

    // 0x00560afc..0x00560b0b: a null or empty name plays nothing (EBP still -1 -> false).
    if (Name == nullptr || Name[0] == '\0')
        return false;

    // 0x00560b11..0x00560b32: the first slot whose id is -1. No free slot leaves -1 and retail
    // writes the name one slot below the array (IMUL -1 * 0x84); the callers queue at most the
    // pilot's body.anim list into a body StopAnimations just emptied.
    int Slot = -1;
    for (int Index = 0; Index < 4; ++Index)
    {
        if (AnimIds[Index] == -1)
        {
            Slot = Index;
            break;
        }
    }

    // 0x00560b36..0x00560b72: the name copied (<= 0x7f chars) into the slot's TString.
    AnimNames[Slot] = Name;

    // 0x00560b75..0x00560b80: gf\CharacterInfo.cpp's script for the stored name.
    void* const Script = GetCharacterInfoDb()->AcquireScriptByName(AnimNames[Slot].Data);

    // 0x00560b85..0x00560bc5: IDeformable slot 7 (0x1c).
    GENRESULT const Result = DALib::Deform->Play(Deformable, &AnimIds[Slot], Script,
                                                 const_cast<char*>(Name), DwordArg(Arg2),
                                                 DwordArg(Arg3), DwordArg(Arg4), DwordArg(1.0f),
                                                 DwordArg(static_cast<unsigned>(Arg5)),
                                                 DwordArg(-1.0f), nullptr);
    // 0x00560bca..0x00560bec: MOV EAX,0x100002 -- FDUMP with the caller's name.
    if (Result != 0)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: failed to play character animation '%s'\n",
              kEngineCharacterH, 0x1f7, Name);
    }
    // 0x00560bef..0x00560bf6: SETZ on Play's result.
    return Result == 0;
}

// ============================================================================================
// FEngineCharacterCache
// ============================================================================================

FEngineCharacterCache G_EngineCharacterCache;

FEngineCharacterCache::FEngineCharacterCache()
{
    // FUN_004c98c0: the sentinel node, size 0.
}

FEngineCharacterCache::~FEngineCharacterCache()
{
    // FUN_004c9900.
    std::list<FEngineCharacterCacheNode>::iterator It = Nodes.begin();
    while (It != Nodes.end())
    {
        FEngineCharacterBody* const Body = It->Body;
        if (Body != nullptr)
        {
            // 0x004c9940..0x004c99a3: FUN_004c9a30, the holders released (FUN_00420f70),
            // IDeformable slot 6, the holders' dtors and the free -- FUN_004ca530 + free, inlined.
            delete Body;
        }
        It->Body = nullptr;
        It = Nodes.erase(It);
    }
}

FEngineCharacterBody* FEngineCharacterCache::FindOrCreate(Costume const& Key)
{
    // FUN_004c9a80. 0x004c9a9b..0x004c9ad3: the first node whose Costume compares equal.
    for (std::list<FEngineCharacterCacheNode>::iterator It = Nodes.begin(); It != Nodes.end();
         ++It)
    {
        if (Costume_is_equal(It->CostumeKey, Key))
        {
            ++It->RefCount;                 // 0x004c9bcd
            return It->Body;
        }
    }

    // 0x004c9ad5..0x004c9b22: the key copied part by part -- the four parts, the count and
    // only the first Count accessory ids.
    FEngineCharacterCacheNode Node;
    Node.CostumeKey.PartIds[0] = Key.PartIds[0];
    Node.CostumeKey.PartIds[1] = Key.PartIds[1];
    Node.CostumeKey.PartIds[2] = Key.PartIds[2];
    Node.CostumeKey.PartIds[3] = Key.PartIds[3];
    Node.CostumeKey.AccessoryCount = Key.AccessoryCount;
    for (unsigned Index = 0; static_cast<int>(Index) < static_cast<int>(Key.AccessoryCount);
         ++Index)
        Node.CostumeKey.AccessoryIds[Index] = Key.AccessoryIds[Index];

    // 0x004c9b24..0x004c9bcb: the body.
    FEngineCharacterBody* Body = new FEngineCharacterBody();

    // 0x004c9bea..0x004c9c09: FUN_004ca4b0, then the deformable from the bodyparts.
    Body->Reset();
    {
        FEngineCharacterBodyDesc Desc(Key);
        Body->Deformable = Desc.CreateDeformable();
    }

    // 0x004c9c0c..0x004c9d3e: the accessories. The index test is `CMP i,4 / JA`, so a costume
    // with five or more accessories reaches index 4 -- one holder past the four the body has,
    // over AnimNames[0]. The costume tables retail ships never do (8 slots, the parser keeps
    // what the ini lists); the port keeps the test as it is.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    FMeshHolder* Holder = &Body->Accessories[0];
    for (int Index = 0; Index < static_cast<int>(Key.AccessoryCount); ++Index, ++Holder)
    {
        if (static_cast<unsigned>(Index) > 4u)
            break;
        accessory const* const Found = Descs->find_accessory(Key.AccessoryIds[Index]);
        if (Found == nullptr)
            continue;

        FEngineCharacterAccessoryLink Link;
        Link.Mesh = CStrOrEmpty(Found->Mesh);
        Link.CharacterHardpoint = CStrOrEmpty(Found->CharacterHardpoint);
        Link.AccessoryHardpoint = CStrOrEmpty(Found->AccessoryHardpoint);
        Holder->EngineInstance = Link.Connect(Body->GetRootBone());

        // 0x004c9cc7..0x004c9cff: with a material library, the first one (CacheString::clear +
        // StringAlloc(name, 1) -- the local is never cleared again) into the holder's first
        // material slot.
        if (!Found->Materials.empty())
        {
            char const* const First = CStrOrEmpty(Found->Materials.front());
            CacheString Material;
            Material.clear();
            Material.Value = StringAlloc(First, true);
            ClientMeshHolderSetMaterials(Holder, Material.Value, nullptr);
        }
    }

    // 0x004c9d44..0x004c9d61: no deformable -> the body is thrown away (FUN_004ca530 + free).
    if (Body->Deformable == nullptr)
    {
        delete Body;
        Body = nullptr;
    }

    // 0x004c9d65..0x004c9d7f: appended with count 1, the null body included.
    Node.Body = Body;
    Node.RefCount = 1;
    Nodes.push_back(Node);
    return Body;
}

void FEngineCharacterCache::DeleteAll()
{
    // 0x004ce99d..0x004ce9d1 (HUD_Receiver's destructor).
    std::list<FEngineCharacterCacheNode>::iterator It = Nodes.begin();
    while (It != Nodes.end())
    {
        if (It->Body != nullptr)
            delete It->Body; // FUN_004ceb30(1): FUN_004ca530 + free
        It->Body = nullptr;
        It = Nodes.erase(It);
    }
}
