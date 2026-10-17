// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RendComp.h"
#include "common_misc.h" // ICamera (IRenderer Slot7 reads the camera)

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <utility>

// Engine aggregate descriptor as FUN_06c34b90 reads it: +8 the controlling outer (the engine,
// null when not aggregated), +0xC where the inner non-delegating IDAComponent is written back,
// +0x14 the system container (FUN_06c32350).
struct FAggDesc18
{
    unsigned long Size;
    const char* Name;
    IDAComponent* Outer;
    void** InnerOut;
    const char* Value;
    IDAComponent* Parent;
};

// IRenderer Slot7 flag bits (LAB_06c32a80).
static const unsigned long kDrawPickSwitch = 0x08;     // pick the LOD by distance (FUN_06c32040)
static const unsigned long kDrawRotateOffset = 0x10;   // rotate the matrix offset by its own 3x3
static const unsigned long kDrawConjugate = 0x20;      // matrix := World * matrix * World^-1
static const unsigned long kDrawMatrixReady = 0x40;    // the matrix is used as given
static const unsigned long kDrawKeepCurrent = 0x80;    // draw the current LOD
static const unsigned long kDrawNoCull = 0x100;        // skip ICamera ClassifySphere, rank 4
static const unsigned long kDrawNoChildren = 0x2000;   // do not recurse into child instances

// The archetype map (retail VC6 std::map<int, FLodSet> at +8, head +0xC, helpers FUN_06c34170 /
// 06c34380 / 06c34ab0 / 06c2a270). Every lookup in the listing is lower_bound plus a key compare,
// i.e. find.
typedef std::map<int, FLodSet> FArchetypeMap;

static int ArchetypeKey(void* Handle)
{
    return (int)((intptr_t)Handle);
}

static void* const kNoArchetype = (void*)((intptr_t)-1);

class FRenderManager : public IEngineComponent, public IRenderer2
{
public:
    FRenderManager();
    ~FRenderManager();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    int DACOM_API Initialize() override;
    int DACOM_API CreateArchetype(void* Arg1, void* Arg2) override;
    int DACOM_API DuplicateArchetype(void* Arg1, void* Arg2) override;
    int DACOM_API DestroyArchetype(void* Arg1) override;
    int DACOM_API QueryArchetype(void* Arg1, const char* Iid, void** Out) override;
    int DACOM_API CreateInstance(void* Arg1, void* Arg2) override;
    int DACOM_API DestroyInstance(void* Arg1) override;
    int DACOM_API UpdateInstance(void* Arg1, void* Arg2) override;
    int DACOM_API QueryInstance(void* Arg1, const char* Iid, void** Out) override;
    int DACOM_API Notify(void* Arg1) override;

    int DACOM_API QueryGeometry(void* A, float Lod, void* C, void* D, void* E, void* F, void* G) override; // formerly Slot3
    int DACOM_API GetBounds(void* Id, float Lod, void* Out) override; // formerly Slot4
    int DACOM_API GetCentroid(void* Id, float Lod, void* Out) override; // formerly Slot5
    int DACOM_API NoOp(void* A, void* B, void* C, void* D, void* E, void* F, void* G) override; // formerly Slot6
    int DACOM_API RenderInstance(void* Camera, void* Inst, float Threshold, unsigned long Flags, void* Matrix,
                        void* Extra) override;
    int DACOM_API GetInstanceLodCount(void* Inst, void* Out) override; // formerly Slot8
    int DACOM_API GetInstanceCurrentLod(void* Inst, void* Out) override; // formerly Slot9
    int DACOM_API SetInstanceLod(void* Inst, int Index) override; // formerly Slot10
    int DACOM_API GetArchetypeLodCount(void* Id, int* Out) override; // formerly Slot11
    int DACOM_API GetArchetypeCurrentLod(void* Id, int* Out) override; // formerly Slot12
    int DACOM_API SetArchetypeLod(void* Id, int Index) override; // formerly Slot13
    int DACOM_API GetArchetypeLodRange(void* Id, int Index, void* OutA, void* OutB) override; // formerly Slot14
    int DACOM_API SetArchetypeLodRange(void* Id, int Index, float A, float B) override; // formerly Slot15
    int DACOM_API GetInstanceLodRange(void* Inst, int Index, void* OutA, void* OutB) override; // formerly Slot16
    int DACOM_API SetInstanceLodRange(void* Inst, int Index, float A, float B) override; // formerly Slot17
    int DACOM_API QueryGeometryAtLod(void* A, void* B, void* C, void* D, void* E, void* F, void* G) override; // formerly Slot18
    int DACOM_API SetArchetypeRootLodSelection(void* Id, void* Unused, unsigned long Flags) override; // formerly Slot19
    int DACOM_API SetInstanceRootLodSelection(void* Id, void* A, void* B) override; // formerly Slot20
    int DACOM_API GetArchetypeRootLodSelection(void* Key, void* Unused, unsigned long* Out) override; // formerly Slot21
    int DACOM_API GetInstanceRootLodSelection(void* Id, void* A, void* B) override; // formerly Slot22

    // Non-delegating inner IDAComponent (retail this+0x34, vtable PTR_LAB_06c368b0:
    // LAB_06c34cb0 / LAB_06c306b0 / LAB_06c34d70). The engine keeps this one in its component
    // list; the outer interfaces above forward QI/AddRef/Release to Outer.
    struct FInner : public IDAComponent
    {
        unsigned long RefCount; // +0x38
        FRenderManager* Owner;  // +0x3C

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
    };

    FArchetypeMap Archetypes; // +0x08
    IDAComponent* Parent;     // +0x28, FUN_06c32350 from AGGDESC+0x14
    IDAComponent* Outer;      // +0x30, AGGDESC+8, or &Inner when not aggregated
    FInner Inner;             // +0x34
    unsigned char bPluginsLoaded;
    FPluginNode PluginSentinel;
    unsigned long PluginCount;

    FLodSet* FindSet(int Id);
    int MultiLevel(int Id, void* File);
    int WalkPlugins(int Id, void* File);
    int LoadPlugins();
    int UnloadPlugins();
    int SetParentFromDesc(FAggDesc18* Desc);
};

// FUN_06c31f30: drop both arrays, then CurrentLod = 0 and LodCount = Count; a non-zero Count
// gets Count entries seeded {-1, 0} (FUN_06c31e40) and Count + 1 switch floats. Retail leaves
// the switch floats uninitialised (operator new); the port's vector zero-fills them.
void LodSetResize(FLodSet& Set, int Count)
{
    Set.Entries.clear();
    Set.Switch.clear();
    Set.CurrentLod = 0;
    Set.LodCount = Count;
    if (Count == 0)
        return;
    FLodEntry Empty;
    Empty.Handle = kNoArchetype;
    Empty.Plugin = nullptr;
    Set.Entries.assign((size_t)Count, Empty);
    Set.Switch.assign((size_t)Count + 1, 0.0f);
}

// FUN_06c31eb0: resize to the source's count, copy every entry and switch value (the last
// switch value too, only when the count is positive), then the flag byte.
void LodSetAssign(FLodSet& Dest, const FLodSet& Src)
{
    LodSetResize(Dest, Src.LodCount);
    if (Dest.LodCount > 0)
    {
        int Index = 0;
        for (; Index < Dest.LodCount; ++Index)
        {
            Dest.Entries[Index].Handle = Src.Entries[Index].Handle;
            Dest.Entries[Index].Plugin = Src.Entries[Index].Plugin;
            Dest.Switch[Index] = Src.Switch[Index];
        }
        Dest.Switch[Index] = Src.Switch[Index];
    }
    Dest.bFlag = Src.bFlag;
}

// FUN_06c32040: Distance < 0 (ordered) -> none. Otherwise the first LOD whose upper switch value
// Switch[i + 1] is >= Distance (FCOMP + TEST AH,1 + JZ: C0 clear, an unordered compare never
// matches); none -> null.
FLodEntry* LodSetPick(FLodSet& Set, float Distance)
{
    if (Distance < 0.0f)
        return nullptr;
    for (int Index = 0; Index < Set.LodCount; ++Index)
    {
        if (Set.Switch[Index + 1] >= Distance)
            return &Set.Entries[Index];
    }
    return nullptr;
}

// FUN_06c32090 (+ the inner seed FUN_06c34b90 writes @ 06c34c00..06c34c10). DAT_06c37d4c
// (G_Engine) is zeroed here; it is a borrowed pointer, see Initialize.
FRenderManager::FRenderManager()
    : Parent(nullptr)
    , Outer(nullptr)
    , bPluginsLoaded(0)
    , PluginCount(0)
{
    Inner.RefCount = 1;
    Inner.Owner = this;
    PluginSentinel.Next = &PluginSentinel;
    PluginSentinel.Prev = &PluginSentinel;
    PluginSentinel.Plugin = nullptr;
    G_Engine = nullptr;
}

FRenderManager::~FRenderManager()
{
    for (FArchetypeMap::const_iterator It = Archetypes.begin(); It != Archetypes.end(); ++It)
    {
        char Line[256];
        _snprintf(Line, sizeof(Line), "RenderMgr: dtor: Archetype %d has dangling render data\n",
                  It->first);
        Line[sizeof(Line) - 1] = '\0';
        RendCompFdumpAt(kFdumpNotice, G_FileRenderMgr, 0xBF, Line); // file = 06c37a38
    }
    ReleaseIf(Parent);
    Parent = nullptr;
    ReleaseIf(G_MaterialLibrary);
    G_MaterialLibrary = nullptr;
    ReleaseIf(G_IndexBuffer);
    G_IndexBuffer = nullptr;
    ReleaseIf(G_VertexBuffer);
    G_VertexBuffer = nullptr;
    ReleaseIf(G_Draw);
    G_Draw = nullptr;
    ReleaseIf(G_Pipeline);
    G_Pipeline = nullptr;
    ReleaseIf(G_VbManager);
    G_VbManager = nullptr;
    G_Engine = nullptr;
    UnloadPlugins();
}

// Outer IEngineComponent / IRenderer2 IUnknown: FUN_06c34db0 / 06c34dc0 / 06c34dd0 and the
// IRenderer2 thunks LAB_06c34e30 / 40 / 50 forward to the controlling outer at +0x30.
GENRESULT DACOM_API FRenderManager::QueryInterface(const char* Iid, void** Out)
{
    return Outer->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FRenderManager::AddRef()
{
    return Outer->AddRef();
}

unsigned long DACOM_API FRenderManager::Release()
{
    return Outer->Release();
}

// The renderer and engine facets share the controlling outer.
struct FRenderManagerIidEntry
{
    const char* Iid;
    int bRenderer; // 1 = the +4 base (IRenderer2), 0 = the +0 base (IEngineComponent)
};

static const FRenderManagerIidEntry G_RenderManagerIidMap[] = {
    {Iids::Renderer2, 1},
    {Iids::Renderer, 1},
    {Iids::EngineComponent, 0},
    {Iids::AggregateComponent, 0},
    {nullptr, 0},
};

GENRESULT DACOM_API FRenderManager::FInner::QueryInterface(const char* Iid, void** Out)
{
    for (int Index = 0; G_RenderManagerIidMap[Index].Iid != nullptr; ++Index)
    {
        if (std::strcmp(G_RenderManagerIidMap[Index].Iid, Iid) != 0)
            continue;
        IDAComponent* Found = nullptr;
        if (G_RenderManagerIidMap[Index].bRenderer != 0)
            Found = (IRenderer2*)Owner;
        else
            Found = (IEngineComponent*)Owner;
        Found->AddRef();
        *Out = Found;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// LAB_06c306b0 (shared with the factory vtable 06c367e8).
unsigned long DACOM_API FRenderManager::FInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// LAB_06c34d70: decrement if non-zero; on zero pin the count at 1 for the teardown, destroy
// the owner (FUN_06c32130 + free) and return 0.
unsigned long DACOM_API FRenderManager::FInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    if (Owner != nullptr)
        delete Owner;
    return 0;
}

// FUN_06c33bf0: Release every plugin (no null test), then unlink and free every node,
// counting down; the loaded flag goes back to 0. Callers: the dtor (06c3225f) and a reload
// in FUN_06c33a10 (06c33a22).
int FRenderManager::UnloadPlugins()
{
    FPluginNode* Walk = PluginSentinel.Next;
    while (Walk != &PluginSentinel)
    {
        Walk->Plugin->Release();
        Walk = Walk->Next;
    }
    Walk = PluginSentinel.Next;
    while (Walk != &PluginSentinel)
    {
        FPluginNode* Node = Walk;
        Walk = Walk->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        PluginCount -= 1;
    }
    bPluginsLoaded = 0;
    return 0;
}

int FRenderManager::LoadPlugins()
{
    if (bPluginsLoaded != 0)
        UnloadPlugins();
    IProfileParser* Parser = nullptr;
    if (DACOM_Acquire()->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) < 0)
    {
        // 06c37c10: the retail message ends in "\n" (unlike the section-not-found one below).
        RendCompFdumpAt(kFdumpWarning, G_FileRenderMgr, 0x563,
                        "RenderMgr: load_engine_components: unable to acquire IID_IProfileParser\n");
        ReleaseIf(Parser);
        return -1;
    }

    void* Section = Parser->OpenSection(G_NameRenderManager, 3);
    if (Section == nullptr)
    {
        RendCompFdumpAt(kFdumpWarning, G_FileRenderMgr, 0x568,
                        "RenderMgr: load_engine_components: [RenderManager] section not found!");
        ReleaseIf(Parser);
        return -1;
    }

    char Line[0x400];
    int LineIndex = 0;
    while (Parser->ReadLine(Section, LineIndex, Line, sizeof(Line)) != 0)
    {
        if (Line[0] != ';')
        {
            char* Cut = Line;
            while (*Cut != '\0' && *Cut != '\t' && *Cut != ' ' && *Cut != ';')
                Cut += 1;
            *Cut = '\0';

            // 06c33a39 / 06c33b28..06c33b38.
            FRenderPluginDesc Desc;
            Desc.Size = 0x10;
            Desc.Name = Line;
            Desc.Parent = Parent;
            Desc.Engine = G_Engine;
            void* Child = nullptr;
            if (DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc), &Child) >= 0)
            {
                // 06c33b5a..06c33b92: push_back, the value copied as is (FUN_06c27640).
                FPluginNode* Node = new FPluginNode();
                Node->Plugin = (IRenderComponent*)Child;
                Node->Next = &PluginSentinel;
                Node->Prev = PluginSentinel.Prev;
                PluginSentinel.Prev->Next = Node;
                PluginSentinel.Prev = Node;
                PluginCount += 1;
            }
        }
        LineIndex += 1;
    }
    Parser->CloseSection(Section);
    bPluginsLoaded = 1;
    ReleaseIf(Parser);
    return 0;
}

int DACOM_API FRenderManager::Initialize()
{
    if (Parent == nullptr)
        return -1;
    if (Parent->QueryInterface(Iids::RenderPipeline8B, (void**)(&G_Pipeline)) < 0)
        RendCompFdumpAt(kFdumpWarning, G_FileRenderMgr, 0xEA, "RenderMgr: Failed to get IID_IRenderPipeline");
    if (G_Pipeline != nullptr)
    {
        if (Parent->QueryInterface(Iids::VertexBufferManager, (void**)(&G_VbManager)) < 0)
            return -1;
        if (G_Pipeline->QueryInterface(Iids::RPVertexBuffer, (void**)(&G_VertexBuffer)) < 0)
            return -1;
        if (G_Pipeline->QueryInterface(Iids::RPIndexBuffer, (void**)(&G_IndexBuffer)) < 0)
            return -1;
        if (G_Pipeline->QueryInterface(Iids::RPDraw, (void**)(&G_Draw)) < 0)
            return -1;
    }
    if (Parent->QueryInterface(Iids::MaterialLibrary, (void**)(&G_MaterialLibrary)) < 0)
        RendCompFdumpAt(kFdumpWarning, G_FileRenderMgr, 0xFE, "RenderMgr: Failed to get IID_IMaterialLibrary");
    if (QueryInterface(Iids::Engine, (void**)(&G_Engine)) < 0)
    {
        RendCompFdumpAt(kFdumpWarning, G_FileRenderMgr, 0x103, "RenderMgr: Initialize: unable to acquire IID_IEngine\n");
        return -1;
    }
    G_Engine->Release();
    return (LoadPlugins() >= 0) ? 0 : -1;
}

FLodSet* FRenderManager::FindSet(int Id)
{
    FArchetypeMap::iterator Found = Archetypes.find(Id);
    if (Found == Archetypes.end())
        return nullptr;
    return &Found->second;
}

int FRenderManager::MultiLevel(int Id, void* FileArg)
{
    IFileSystem* File = (IFileSystem*)FileArg;
    FFileSystemDesc Desc;
    FileDescInit(&Desc, "MultiLevel", "FileSystem");
    IFileSystem* Child = nullptr;
    FArchetypeMap::iterator Found = Archetypes.find(Id);
    if (Found == Archetypes.end() ||
        File->CreateInstance((AGGDESC*)(&Desc), (void**)(&Child)) < 0)
    {
        if (Child != nullptr)
            Child->Release();
        return -1;
    }
    FLodSet& Set = Found->second;

    FFileSystemDesc SwitchDesc;
    FileDescInit(&SwitchDesc, "Switch2", "FileSystem");
    unsigned long SwitchCount = 0;
    float* SwitchData = nullptr;
    const int SwitchHandle = Child->OpenByDesc(&SwitchDesc);
    if (SwitchHandle != -1)
    {
        const unsigned long Bytes = Child->GetSize((unsigned long)SwitchHandle, 0);
        SwitchCount = Bytes >> 2;
        // Retail allocates Bytes / 4 * 4 and reads Bytes (06c33d74..06c33d9d).
        SwitchData = (float*)new unsigned char[SwitchCount * sizeof(float)];
        unsigned long ReadOut = 0;
        if (Child->Read((unsigned long)SwitchHandle, SwitchData, Bytes, &ReadOut, 0) == 0)
        {
            delete[] (const unsigned char*)SwitchData;
            SwitchCount = 0;
        }
        Child->CloseFile((unsigned long)SwitchHandle);
    }
    if (SwitchCount > 0)
    {
        LodSetResize(Set, (int)(SwitchCount - 1));
        std::memcpy(Set.Switch.data(), SwitchData, SwitchCount * sizeof(float));
        delete[] (const unsigned char*)SwitchData;
    }
    else
    {
        LodSetResize(Set, 1);
    }

    char LevelName[16] = "Level";
    IFileSystem* Level = nullptr;
    const int LevelCount = Set.LodCount;
    int Index = 0;
    bool bFailed = false;
    for (; Index < LevelCount; ++Index)
    {
        _itoa(Index, LevelName + 5, 10);
        FFileSystemDesc LevelDesc;
        FileDescInit(&LevelDesc, LevelName, "FileSystem");
        bool bClaimed = false;
        if (Level != nullptr)
        {
            Level->Release();
            Level = nullptr;
        }
        if (Child->CreateInstance((AGGDESC*)(&LevelDesc), (void**)(&Level)) < 0 ||
            PluginSentinel.Next == &PluginSentinel)
        {
            bFailed = true;
            break;
        }
        for (FPluginNode* Walk = PluginSentinel.Next; Walk != &PluginSentinel; Walk = Walk->Next)
        {
            const int Claimed = Walk->Plugin->Claim(Level);
            if (Claimed != -1)
            {
                Set.Entries[Index].Handle = (void*)((intptr_t)Claimed);
                Set.Entries[Index].Plugin = Walk->Plugin;
                bClaimed = true;
            }
        }
        if (!bClaimed)
        {
            bFailed = true;
            break;
        }
    }

    if (bFailed)
    {
        for (int Undo = Index - 1; Undo >= 0; --Undo)
            Set.Entries[Undo].Plugin->DestroyHandle(Set.Entries[Undo].Handle);
        LodSetResize(Set, 0);
        if (Level != nullptr)
            Level->Release();
        Child->Release();
        return -1;
    }
    if (Level != nullptr)
        Level->Release();
    Child->Release();
    return 0;
}

// FUN_06c33fe0 (thiscall, RET 8: Id, File). The archetype's set must be in the map (else -1).
// The first plugin in list order whose Claim (+0xC) is not -1 becomes the only LOD: one entry
// {handle, plugin}, switch {0, 1e6} (0x49742400). No claimant -> -1.
int FRenderManager::WalkPlugins(int Id, void* File)
{
    FArchetypeMap::iterator Found = Archetypes.find(Id);
    if (Found == Archetypes.end())
        return -1;
    for (FPluginNode* Walk = PluginSentinel.Next; Walk != &PluginSentinel; Walk = Walk->Next)
    {
        const int Claimed = Walk->Plugin->Claim(File);
        if (Claimed == -1)
            continue;
        FLodSet& Set = Found->second;
        LodSetResize(Set, 1);
        Set.Switch[0] = 0.0f;
        Set.Switch[1] = G_DefaultLodDistance;
        Set.Entries[0].Handle = (void*)((intptr_t)Claimed);
        Set.Entries[0].Plugin = Walk->Plugin;
        return 0;
    }
    return -1;
}

// LAB_06c32520 (IEngineComponent slot 4, RET 0xC: Id, File). An empty set is inserted under Id;
// an Id already present -> 0. MultiLevel, then the single-LOD plugin walk; either succeeding ->
// 1. Both failing erase the entry again -> 0.
int DACOM_API FRenderManager::CreateArchetype(void* Arg1, void* Arg2)
{
    const int Id = ArchetypeKey(Arg1);
    std::pair<FArchetypeMap::iterator, bool> Inserted =
        Archetypes.insert(FArchetypeMap::value_type(Id, FLodSet()));
    if (!Inserted.second)
        return 0;
    if (MultiLevel(Id, Arg2) >= 0)
        return 1;
    if (WalkPlugins(Id, Arg2) >= 0)
        return 1;
    Archetypes.erase(Id);
    return 0;
}

int DACOM_API FRenderManager::DuplicateArchetype(void* Arg1, void* Arg2)
{
    FArchetypeMap::iterator Source = Archetypes.find(ArchetypeKey(Arg2));
    if (Source == Archetypes.end())
        return 0;
    std::pair<FArchetypeMap::iterator, bool> Inserted =
        Archetypes.insert(FArchetypeMap::value_type(ArchetypeKey(Arg1), FLodSet()));
    if (!Inserted.second)
        return 0;
    FLodSet& Dest = Inserted.first->second;
    LodSetAssign(Dest, Source->second);
    const int Count = Source->second.LodCount;
    for (int Index = 0; Index < Count; ++Index)
    {
        FLodEntry& Entry = Dest.Entries[Index];
        Entry.Handle = (void*)((intptr_t)(
            Entry.Plugin->CloneHandle(Source->second.Entries[Index].Handle)));
    }
    return 0;
}

// LAB_06c327a0 (IEngineComponent slot 6, RET 8: Id). Missing -> nothing. Every LOD handle goes to
// its plugin's DestroyHandle (+0x14), then the map entry is erased. Retail returns EAX as left by
// the erase helper; the port returns 0.
int DACOM_API FRenderManager::DestroyArchetype(void* Arg1)
{
    FArchetypeMap::iterator Found = Archetypes.find(ArchetypeKey(Arg1));
    if (Found == Archetypes.end())
        return 0;
    FLodSet& Set = Found->second;
    const int Count = Set.LodCount;
    for (int Index = 0; Index < Count; ++Index)
        Set.Entries[Index].Plugin->DestroyHandle(Set.Entries[Index].Handle);
    Archetypes.erase(Found);
    return 0;
}

int DACOM_API FRenderManager::QueryArchetype(void* Arg1, const char* Iid, void** Out)
{
    FLodSet* Set = FindSet(ArchetypeKey(Arg1));
    if (Set == nullptr)
        return -1;
    if (Set->CurrentLod == Set->LodCount)
    {
        *Out = nullptr;
        return (int)0xFFFFFFF8;
    }
    FLodEntry& Entry = Set->Entries[Set->CurrentLod];
    return Entry.Plugin->QueryHandle(Entry.Handle, Iid, Out);
}

int DACOM_API FRenderManager::CreateInstance(void* Arg1, void* Arg2)
{
    FLodSet* Set = FindSet(ArchetypeKey(Arg2));
    if (Set == nullptr)
        return 0;
    const int Count = Set->LodCount;
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Set->Entries[Index].Plugin->CreateMeshInstance(Arg1, Set->Entries[Index].Handle) != 0)
            continue;
        for (int Undo = Index - 1; Undo >= 0; --Undo)
            Set->Entries[Undo].Plugin->DestroyMeshInstance(Arg1, Set->Entries[Undo].Handle);
        return 0;
    }
    return 1;
}

int DACOM_API FRenderManager::DestroyInstance(void* Arg1)
{
    void* Arch = G_Engine->GetInstanceArchetype(Arg1);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    FLodSet* Set = FindSet(ArchetypeKey(Arch));
    if (Set == nullptr)
        return 0;
    const int Count = Set->LodCount;
    for (int Index = 0; Index < Count; ++Index)
        Set->Entries[Index].Plugin->DestroyMeshInstance(Arg1, Set->Entries[Index].Handle);
    return 0;
}

// LAB_06c32a40 (IEngineComponent slot 10): every plugin's Slot9 (+0x24) with both arguments.
int DACOM_API FRenderManager::UpdateInstance(void* Arg1, void* Arg2)
{
    for (FPluginNode* Walk = PluginSentinel.Next; Walk != &PluginSentinel; Walk = Walk->Next)
        Walk->Plugin->UpdateMeshInstance(Arg1, Arg2);
    return 0;
}

int DACOM_API FRenderManager::QueryInstance(void* Arg1, const char* Iid, void** Out)
{
    void* Arch = G_Engine->GetInstanceArchetype(Arg1);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    FLodSet* Set = FindSet(ArchetypeKey(Arch));
    if (Set == nullptr)
        return -1;
    if (Set->CurrentLod == Set->LodCount)
    {
        *Out = nullptr;
        return (int)0xFFFFFFF8;
    }
    FLodEntry& Entry = Set->Entries[Set->CurrentLod];
    return Entry.Plugin->QueryMeshInstance(Arg1, Entry.Handle, Iid, Out);
}

// LAB_06c324f0 (IEngineComponent slot 12): every plugin's Slot11 (+0x2c) with the argument.
int DACOM_API FRenderManager::Notify(void* Arg1)
{
    for (FPluginNode* Walk = PluginSentinel.Next; Walk != &PluginSentinel; Walk = Walk->Next)
        Walk->Plugin->UpdateMeshInstances(Arg1);
    return 0;
}

// formerly Slot3
int DACOM_API FRenderManager::QueryGeometry(void* A, float Lod, void* C, void* D, void* E, void* F, void* G)
{
    FLodSet* Set = FindSet(ArchetypeKey(A));
    if (Set == nullptr)
        return 0;
    FLodEntry* Entry = LodSetPick(*Set, Lod);
    if (Entry == nullptr)
        return 0;
    int LodBits = 0;
    std::memcpy(&LodBits, &Lod, sizeof(LodBits));
    return Entry->Plugin->QueryGeometry(Entry->Handle, LodBits, C, D, E, F, G) & 0xFF;
}

// LAB_06c33350 (IRenderer slot 4, RET 0x10). Archetype missing or no LOD for Lod -> 0; else the
// LOD's plugin Slot13 (+0x34)(Handle, Lod, Out), low byte.
// formerly Slot4
int DACOM_API FRenderManager::GetBounds(void* Id, float Lod, void* Out)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr)
        return 0;
    FLodEntry* Entry = LodSetPick(*Set, Lod);
    if (Entry == nullptr)
        return 0;
    return Entry->Plugin->GetBounds(Entry->Handle, Lod, Out) & 0xFF;
}

// LAB_06c333d0 (IRenderer slot 5, RET 0x10). As slot 4 with the plugin's Slot14 (+0x38).
// formerly Slot5
int DACOM_API FRenderManager::GetCentroid(void* Id, float Lod, void* Out)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr)
        return 0;
    FLodEntry* Entry = LodSetPick(*Set, Lod);
    if (Entry == nullptr)
        return 0;
    return Entry->Plugin->GetCentroid(Entry->Handle, Lod, Out) & 0xFF;
}

// LAB_06c33450: XOR EAX,EAX; RET 0x20.
// formerly Slot6
int DACOM_API FRenderManager::NoOp(void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/, void* /*E*/, void* /*F*/,
                                    void* /*G*/)
{
    return 0;
}

// formerly Slot7
int DACOM_API FRenderManager::RenderInstance(void* CameraArg, void* Inst, float Threshold, unsigned long Flags,
                                    void* MatrixArg, void* Extra)
{
    if (Threshold < 0.0f)
        return 1;

    ICamera* Camera = (ICamera*)CameraArg;
    const FTransform* World = (const FTransform*)(G_Engine->GetInstanceTransform(Inst));
    const FTransform* Matrix = (const FTransform*)MatrixArg;

    float Scale = 1.0f;
    if (Matrix != nullptr)
    {
        const float* M = (const float*)Matrix;
        const float Len2 = (float)(std::sqrt((double)M[8] * M[8] +
                                                        (double)M[5] * M[5] +
                                                        (double)M[2] * M[2]));
        const float Len1 = (float)(std::sqrt((double)M[7] * M[7] +
                                                        (double)M[4] * M[4] +
                                                        (double)M[1] * M[1]));
        const double Len0Wide = std::sqrt((double)M[6] * M[6] +
                                          (double)M[3] * M[3] +
                                          (double)M[0] * M[0]);
        const float Len0 = (float)Len0Wide;
        const float Larger = (Len0Wide < (double)Len1) ? Len1 : Len0;
        Scale = (Larger < Len2) ? Len2 : Larger;
    }

    FTransform Composed;
    const FTransform* DrawMatrix = Matrix;
    if (Matrix != nullptr && (Flags & kDrawMatrixReady) == 0)
    {
        const FTransform* Source = Matrix;
        FTransform Rotated;
        if ((Flags & kDrawRotateOffset) != 0)
        {
            FVector Offset;
            MathEngineCached(G_MathRenderMgr)->Rotate(Offset, *Matrix, Matrix->Translation);
            Rotated.Rotation = Matrix->Rotation;
            Rotated.Translation = Offset;
            Source = &Rotated;
            Flags &= ~kDrawRotateOffset;
        }
        Composed = *Source;
        if ((Flags & kDrawConjugate) != 0)
        {
            FVector Offset;
            TransformPoint3(&Offset.X, (const float*)World, &World->Translation.X);
            FTransform Inverse;
            VectorNegate3(&Offset.X, &Inverse.Translation.X);
            TransposeXform3x3((const float*)World, &Inverse.Rotation.D[0][0]);
            FTransform WorldTimesMatrix;
            MathEngineCached(G_MathRenderMgr)->MulTransform(WorldTimesMatrix, *World, Composed);
            const FTransform Left = WorldTimesMatrix;
            FTransform Conjugated;
            MathEngineCached(G_MathRenderMgr)->MulTransform(Conjugated, Left, Inverse);
            Composed = Conjugated;
            Flags &= ~kDrawConjugate;
        }
        Flags |= kDrawMatrixReady;
        DrawMatrix = &Composed;
    }

    float Radius = 0.0f;
    FVector Center;
    G_Engine->GetInstanceExtent(Inst, 1, &Radius, &Center.X);
    if (Radius < 0.001)
        Radius = 1.0f;
    FVector WorldCenter;
    MathEngineCached(G_MathRenderMgr)->TransformXform(WorldCenter, *World, Center);
    const FTransform* View = (const FTransform*)(Camera->GetViewTransform());
    FVector ViewCenter;
    MathEngineCached(G_MathRenderMgr)->TransformXform(ViewCenter, *View, WorldCenter);
    const float Depth = (ViewCenter.Z < 0.0f) ? -ViewCenter.Z : ViewCenter.Z;

    int Rank = 4;
    bool bDraw = true;
    if ((Flags & kDrawNoCull) == 0)
    {
        Rank = Camera->ClassifySphere(&ViewCenter.X, Radius * Scale);
        bDraw = Rank >= 3;
    }

    if (bDraw)
    {
        void* Arch = G_Engine->GetInstanceArchetype(Inst);
        if (Arch != kNoArchetype)
        {
            G_Engine->ReleaseHandle(Arch);
            FLodSet* Set = FindSet(ArchetypeKey(Arch));
            if (Set != nullptr)
            {
                unsigned long SubmitFlags = Flags;
                FLodEntry* Entry = nullptr;
                if ((Flags & kDrawKeepCurrent) != 0)
                {
                    if (Set->CurrentLod < Set->LodCount)
                        Entry = Set->Entries.data() + Set->CurrentLod;
                }
                else if ((Flags & kDrawPickSwitch) == 0)
                {
                    Entry = Set->Entries.data();
                }
                else
                {
                    if (Set->bFlag != 0)
                        G_Engine->GetInstanceRoot(Inst);
                    Camera->GetHalfFovXDeg();
                    const int* Viewport = Camera->GetViewport();
                    const float HalfWidth = (float)((Viewport[2] - Viewport[0] + 1) / 2);
                    const float XScale = (float)(Camera->GetXScaleOverNear()); // FSTP float
                    const double ZNear = Camera->GetZNear();
                    const float Distance = (float)(
                        (double)HalfWidth / (ZNear * XScale) / Scale * Depth * Threshold);
                    SubmitFlags &= ~kDrawPickSwitch;
                    Entry = LodSetPick(*Set, Distance);
                }

                if (Entry != nullptr)
                {
                    Set->CurrentLod = (int)(Entry - Set->Entries.data());
                    Entry->Plugin->BindMeshInstance(Inst, Entry->Handle);
                    Entry->Plugin->RenderInstance(Camera, Entry->Handle, Inst, SubmitFlags,
                                          (FTransform*)DrawMatrix, Extra);
                }
                else
                {
                    Set->CurrentLod = Set->LodCount;
                    Rank = 1;
                }
            }
        }
    }

    if ((Flags & kDrawNoChildren) == 0)
    {
        void* Child = G_Engine->GetRelatedInstance(Inst, 1, kNoArchetype);
        while (Child != kNoArchetype)
        {
            const int ChildRank = RenderInstance(Camera, Child, Threshold, Flags,
                                        (FTransform*)DrawMatrix, Extra);
            if (Rank != ChildRank)
            {
                const int Larger = (Rank < ChildRank) ? ChildRank : Rank;
                Rank = (Larger > 3) ? 3 : Larger;
            }
            Child = G_Engine->GetRelatedInstance(Inst, 1, Child);
        }
    }
    return Rank;
}

// LAB_06c33460 (IRenderer slot 8, RET 0xC). Instance archetype (IEngine +0x64, -1 -> -1,
// ReleaseHandle +0x3c) forwarded to slot 11 (+0x2c): the LOD count.
// formerly Slot8
int DACOM_API FRenderManager::GetInstanceLodCount(void* Inst, void* Out)
{
    void* Arch = G_Engine->GetInstanceArchetype(Inst);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    return GetArchetypeLodCount(Arch, (int*)Out);
}

// LAB_06c334b0 (IRenderer slot 9, RET 0xC). As slot 8, forwarded to slot 12 (+0x30).
// formerly Slot9
int DACOM_API FRenderManager::GetInstanceCurrentLod(void* Inst, void* Out)
{
    void* Arch = G_Engine->GetInstanceArchetype(Inst);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    return GetArchetypeCurrentLod(Arch, (int*)Out);
}

// formerly Slot10
int DACOM_API FRenderManager::SetInstanceLod(void* Inst, int Index)
{
    void* Arch = G_Engine->GetInstanceArchetype(Inst);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    FLodSet* Set = FindSet(ArchetypeKey(Arch));
    if (Set == nullptr)
        return -1;
    int Result = -1;
    if (Index < Set->LodCount)
    {
        Result = Set->Entries[Index].Plugin->BindMeshInstance(Inst, Set->Entries[Index].Handle);
        if (Result == 0)
            Set->CurrentLod = Index;
    }
    else if (Index == Set->LodCount)
    {
        Set->CurrentLod = Index;
    }
    return Result;
}

// LAB_06c335b0 (IRenderer slot 11, RET 0xC): set missing -> -1; *Out = LodCount, 0.
// formerly Slot11
int DACOM_API FRenderManager::GetArchetypeLodCount(void* Id, int* Out)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr)
        return -1;
    *Out = Set->LodCount;
    return 0;
}

// LAB_06c33610 (IRenderer slot 12, RET 0xC): set missing -> -1; *Out = CurrentLod, 0.
// formerly Slot12
int DACOM_API FRenderManager::GetArchetypeCurrentLod(void* Id, int* Out)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr)
        return -1;
    *Out = Set->CurrentLod;
    return 0;
}

// LAB_06c33670 (IRenderer slot 13, RET 0xC): set missing or Index > LodCount (signed) -> -1;
// CurrentLod = Index, 0.
// formerly Slot13
int DACOM_API FRenderManager::SetArchetypeLod(void* Id, int Index)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr || Index > Set->LodCount)
        return -1;
    Set->CurrentLod = Index;
    return 0;
}

// LAB_06c33780 (IRenderer slot 14, RET 0x14): set missing or Index >= LodCount -> -1;
// *OutA = Switch[Index], *OutB = Switch[Index + 1], 0.
// formerly Slot14
int DACOM_API FRenderManager::GetArchetypeLodRange(void* Id, int Index, void* OutA, void* OutB)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr || Index >= Set->LodCount)
        return -1;
    *(float*)OutA = Set->Switch[Index];
    *(float*)OutB = Set->Switch[Index + 1];
    return 0;
}

// LAB_06c33800 (IRenderer slot 15, RET 0x14): A > B or unordered (TEST AH,0x41 + JP) -> -1; set
// missing or Index >= LodCount -> -1; Switch[Index] = A, Switch[Index + 1] = B, 0.
// formerly Slot15
int DACOM_API FRenderManager::SetArchetypeLodRange(void* Id, int Index, float A, float B)
{
    if (!(A <= B))
        return -1;
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr || Index >= Set->LodCount)
        return -1;
    Set->Switch[Index] = A;
    Set->Switch[Index + 1] = B;
    return 0;
}

// LAB_06c336e0 (IRenderer slot 16, RET 0x14): instance archetype as slot 8, forwarded to slot 14.
// formerly Slot16
int DACOM_API FRenderManager::GetInstanceLodRange(void* Inst, int Index, void* OutA, void* OutB)
{
    void* Arch = G_Engine->GetInstanceArchetype(Inst);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    return GetArchetypeLodRange(Arch, Index, OutA, OutB);
}

// LAB_06c33730 (IRenderer slot 17, RET 0x14): instance archetype as slot 8, forwarded to slot 15.
// formerly Slot17
int DACOM_API FRenderManager::SetInstanceLodRange(void* Inst, int Index, float A, float B)
{
    void* Arch = G_Engine->GetInstanceArchetype(Inst);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    return SetArchetypeLodRange(Arch, Index, A, B);
}

// LAB_06c332b0 (IRenderer slot 18, RET 0x20): set missing or B >= LodCount (signed) -> 0;
// else LOD B's plugin Slot12 (+0x30) with (Handle, 1.0f bits, C, D, E, F, G), low byte.
// formerly Slot18
int DACOM_API FRenderManager::QueryGeometryAtLod(void* A, void* B, void* C, void* D, void* E, void* F, void* G)
{
    FLodSet* Set = FindSet(ArchetypeKey(A));
    if (Set == nullptr)
        return 0;
    const int LodIndex = ArchetypeKey(B);
    if (LodIndex >= Set->LodCount)
        return 0;
    FLodEntry& Entry = Set->Entries[LodIndex];
    return Entry.Plugin->QueryGeometry(Entry.Handle, 0x3f800000, C, D, E, F, G) & 0xFF;
}

// FUN_06c33890 (IRenderer slot 19, RET 0x10): set missing -> -1; flag byte = bit 0 of the low
// byte of Flags, 0.
// formerly Slot19
int DACOM_API FRenderManager::SetArchetypeRootLodSelection(void* Id, void* /*Unused*/, unsigned long Flags)
{
    FLodSet* Set = FindSet(ArchetypeKey(Id));
    if (Set == nullptr)
        return -1;
    Set->bFlag = (unsigned char)((Flags & 1) != 0 ? 1 : 0);
    return 0;
}

// LAB_06c33900 (IRenderer slot 20, RET 0x10): instance archetype as slot 8, forwarded to slot 19.
// formerly Slot20
int DACOM_API FRenderManager::SetInstanceRootLodSelection(void* Id, void* A, void* B)
{
    void* Arch = G_Engine->GetInstanceArchetype(Id);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    return SetArchetypeRootLodSelection(Arch, A, (unsigned long)((uintptr_t)B));
}

// LAB_06c33950 (IRenderer slot 21, RET 0x10): *Out = 0 first; set missing -> -1; flag on ->
// *Out |= 1; 0.
// formerly Slot21
int DACOM_API FRenderManager::GetArchetypeRootLodSelection(void* Key, void* /*Unused*/, unsigned long* Out)
{
    *Out = 0;
    FLodSet* Set = FindSet(ArchetypeKey(Key));
    if (Set == nullptr)
        return -1;
    if (Set->bFlag != 0)
        *Out |= 1;
    return 0;
}

// LAB_06c339c0 (IRenderer slot 22, RET 0x10): instance archetype as slot 8, forwarded to slot 21.
// formerly Slot22
int DACOM_API FRenderManager::GetInstanceRootLodSelection(void* Id, void* A, void* B)
{
    void* Arch = G_Engine->GetInstanceArchetype(Id);
    if (Arch == kNoArchetype)
        return -1;
    G_Engine->ReleaseHandle(Arch);
    return GetArchetypeRootLodSelection(Arch, A, (unsigned long*)B);
}

// FUN_06c32350: the system container from AGGDESC+0x14, AddRef'd; none -> -2.
int FRenderManager::SetParentFromDesc(FAggDesc18* Desc)
{
    if (Desc == nullptr || Desc->Parent == nullptr)
        return GR_INVALID_PARMS;
    Parent = Desc->Parent;
    Parent->AddRef();
    return GR_OK;
}

GENRESULT DACOM_API RenderManagerFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    FAggDesc18* Extended = (FAggDesc18*)Desc;
    if (Extended->Size != 0x18 || std::strcmp(Extended->Name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FRenderManager* Object = new (std::nothrow) FRenderManager();
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    Object->Outer = Extended->Outer;
    if (Object->Outer != nullptr)
        *Extended->InnerOut = (IDAComponent*)(&Object->Inner);
    else
        Object->Outer = &Object->Inner;

    const int Result = Object->SetParentFromDesc(Extended);
    if (Result != GR_OK)
    {
        if (Extended->InnerOut != nullptr)
            *Extended->InnerOut = nullptr;
        delete Object;
        *Out = nullptr;
        return Result;
    }
    *Out = (IEngineComponent*)Object;
    return GR_OK;
}
