// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Engine-instance hierarchy helpers: Hierarchy::GetDepth, IsDescendant, GetRoot, FindChild,
// the CompoundInstance* walkers, hardpoint queries (HardpointPosition[A], HardpointToObject,
// NumHardpoints) and JointEnumCallback.
//
// Engine instance handles are `long` here (-1 = none) and `void*` in the IEngine / IHardpoint
// interfaces; the C-style casts between the two are value conversions of the same handle.
// Vector / Matrix / Transform are layout-identical to x86math's FVector / FMatrix / FTransform.
#include "common_cobject.h"
#include "x86math.h"

#include "DALib.h"
#include "dacom.h"

#include <new>
#include <string.h>

int __cdecl Hierarchy::GetDepth(long ParentInst, long ChildInst)
{
    // .text:0630e6d0..0630e707. Depth 0; child -1 -> -1; walk: child == parent returns the
    // depth, else ++depth and child = Engine->GetInstanceParent(child) (slot +0xd4, no null
    // test on DALib::Engine) until -1, which returns -1.
    int Depth = 0;
    while (ChildInst != -1)
    {
        if (ChildInst == ParentInst)
            return Depth;
        ++Depth;
        ChildInst = (long)DALib::Engine->GetInstanceParent((void*)ChildInst);
    }
    return -1;
}

bool __cdecl IsDescendant(long ParentInst, long ChildInst)
{
    // .text:0630e710..0630e74a: the GetDepth loop inlined, result depth != -1. A child of
    // -1 is never a descendant, even of a parent of -1.
    int Depth = 0;
    while (true)
    {
        if (ChildInst == -1)
        {
            Depth = -1;
            break;
        }
        if (ChildInst == ParentInst)
            break;
        ++Depth;
        ChildInst = (long)DALib::Engine->GetInstanceParent((void*)ChildInst);
    }
    return Depth != -1;
}

bool __cdecl HardpointPosition(long Instance, char const* HpName, Vector* OutPos, Matrix* OutOrient)
{
    // Common.dll.txt 0630ef30-0630f068. FUN_0630e870 (= FindHardpoint_OS, the search over
    // Instance and its compound children) finds HpName and the instance it sits on; a miss
    // returns false. That instance's world position (Engine +0x90) and orientation (Engine
    // +0x98) are read; OutPos = orientation * hardpoint position (3DMathEngine +0x2c) +
    // instance position, OutOrient = orientation * hardpoint orientation (+0x20). Each output
    // is written only when non-null.
    long FoundOn;
    HardpointInfo Info;
    if (!FindHardpoint_OS(Instance, HpName, FoundOn, Info))
        return false;

    float const* const InstancePos = DALib::Engine->GetInstancePosition((void*)FoundOn);
    float const PosX = InstancePos[0];
    float const PosY = InstancePos[1];
    float const PosZ = InstancePos[2];
    FMatrix const InstanceOrient =
        *(FMatrix const*)DALib::Engine->GetInstanceOrientation((void*)FoundOn);

    if (OutPos != nullptr)
    {
        FVector Rotated;
        Acquire3DMathEngine()->TransformMatrix(Rotated, InstanceOrient,
                                               (FVector const&)Info.Position);
        OutPos->x = Rotated.X + PosX;
        OutPos->y = Rotated.Y + PosY;
        OutPos->z = Rotated.Z + PosZ;
    }
    if (OutOrient != nullptr)
    {
        FMatrix Combined;
        Acquire3DMathEngine()->MulMatrix(Combined, InstanceOrient, (FMatrix const&)Info.Orient);
        *OutOrient = (Matrix const&)Combined;
    }
    return true;
}

// HardpointPositionA (0630eda0) is defined after JointEnumCallback below: it walks the joint
// list that callback builds.

// DACOM.DLL!stricmp through the isolated TU common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // Engine slot 54 (+0xd8) with flag 1: the next child of Instance after Previous (-1 = the
    // first); -1 when there are no more.
    long NextChildInstance(long Instance, long Previous)
    {
        return (long)DALib::Engine->GetRelatedInstance((void*)Instance, 1, (void*)Previous);
    }
}

void __cdecl CompoundInstanceAnalyze(long Instance, void (__cdecl* Callback)(long, void*), void* Context)
{
    // Common.dll.txt 063034f0-06303541: Callback(Instance, Context), then for every child
    // (Engine +0xd8, flag 1, starting from -1) recurse. No checks of Callback, Instance or
    // DALib::Engine.
    Callback(Instance, Context);
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, Callback, Context);
    } while (Child != -1);
}

// FUN_06303550 (06303550-06303556): ++*(int*)Context.
static void __cdecl CompoundInstanceCount_AnalyzeCallback(long /*Instance*/, void* Context)
{
    int* const Count = (int*)Context;
    ++*Count;
}

int __cdecl CompoundInstanceCount(long Instance)
{
    // Common.dll.txt 06303560-063035ae: Count = 1 (the root), then CompoundInstanceAnalyze
    // over each direct child with FUN_06303550.
    int Count = 1;
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, CompoundInstanceCount_AnalyzeCallback, &Count);
    } while (Child != -1);
    return Count;
}

namespace
{
    // FUN_063036d0 (063036d0-06303713): Engine slot 26 (+0x68) fills an 8-byte record and
    // returns a pointer to it; its first word is the instance name, null read as ""
    // (DAT_063997b0). True when DACOM stricmp(name, Name) == 0.
    bool __cdecl CompoundInstanceNameMatches(long Instance, char const* Name)
    {
        FEngineName Info = {nullptr, 0};
        FEngineName* const Result = DALib::Engine->GetInstanceName(&Info, (void*)Instance);
        char const* Found = Result->Text;
        if (Found == nullptr)
            Found = "";
        return CallDacomStricmp(Found, Name) == 0;
    }

    // The 3-dword context of CompoundInstanceFindFirstName (06303731-06303741 / 063037a6-
    // 063037b2): one dword that is the match counter on the first walk and the write cursor
    // on the second, the predicate (always FUN_063036d0), and its Name argument.
    struct FNameScan
    {
        union
        {
            int* MatchCount;
            long* Cursor;
        };
        bool (__cdecl* Predicate)(long, char const*);
        char const* Name;
    };

    // FUN_06303680 (06303680-0630369b): Predicate(Instance, Name) -> ++count.
    void __cdecl CountMatchingName(long Instance, void* Context)
    {
        FNameScan* const Scan = (FNameScan*)Context;
        if (Scan->Predicate(Instance, Scan->Name))
            *Scan->MatchCount += 1;
    }

    // FUN_063036a0 (063036a0-063036c2): Predicate(Instance, Name) -> *cursor = Instance,
    // cursor += 4.
    void __cdecl AppendMatchingName(long Instance, void* Context)
    {
        FNameScan* const Scan = (FNameScan*)Context;
        if (!Scan->Predicate(Instance, Scan->Name))
            return;
        *Scan->Cursor = Instance;
        Scan->Cursor += 1;
    }

    // FUN_063035b0 (063035b0-063035bf): **Context = Instance, *Context += 4.
    void __cdecl AppendInstance(long Instance, void* Context)
    {
        long** const Cursor = (long**)Context;
        **Cursor = Instance;
        *Cursor += 1;
    }
}

void __cdecl CompoundInstanceList(long Instance, int* OutCount, long** OutArray)
{
    // Common.dll.txt 063035c0-06303679: Count = 1 + every descendant (the CompoundInstanceCount
    // walk, inlined); *OutCount = Count; Array = operator new(Count * 4); *OutArray = Array;
    // Array[0] = Instance; then each direct child's subtree is appended through FUN_063035b0
    // with the cursor at Array + 1. No null checks of OutCount / OutArray. The caller owns the
    // array and frees it with operator delete, hence the raw allocation.
    int Count = 1;
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, CompoundInstanceCount_AnalyzeCallback, &Count);
    } while (Child != -1);

    *OutCount = Count;
    long* const Array = (long*)::operator new((size_t)Count * sizeof(long));
    *OutArray = Array;
    Array[0] = Instance;

    long* Cursor = Array + 1;
    Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, AppendInstance, &Cursor);
    } while (Child != -1);
}

long __cdecl CompoundInstanceFindFirstName(long Instance, char const* Name)
{
    // Common.dll.txt 06303720-06303837. First walk: the root through FUN_063036d0 directly,
    // then each child's subtree through FUN_06303680 - counts the matches. Array =
    // operator new(Count * 4). Second walk: the same with FUN_063036a0, filling Array.
    // Count == 0 -> delete Array, return -1 (ESI, the child walk's final value); else
    // return Array[0] after deleting it. No null check of Name.
    FNameScan Scan;
    int Count = 0;
    Scan.MatchCount = &Count;
    Scan.Predicate = CompoundInstanceNameMatches;
    Scan.Name = Name;
    if (CompoundInstanceNameMatches(Instance, Name))
        ++Count;
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, CountMatchingName, &Scan);
    } while (Child != -1);

    long* const Array = new long[Count];
    FNameScan Fill;
    Fill.Cursor = Array;
    Fill.Predicate = CompoundInstanceNameMatches;
    Fill.Name = Name;
    if (CompoundInstanceNameMatches(Instance, Name))
    {
        *Fill.Cursor = Instance;
        Fill.Cursor += 1;
    }
    Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, AppendMatchingName, &Fill);
    } while (Child != -1);

    if (Count == 0)
    {
        delete[] Array;
        return Child;
    }
    long const Result = Array[0];
    delete[] Array;
    return Result;
}

// long __cdecl FindChild(long,char const*) -- .text:0630e750-0630e7ed, exported (ordinal 1439).
// DFS over the engine child hierarchy (Engine slot 0xd8 GetRelatedInstance with flag 1, first
// call with -1, then with the previous child) for the first descendant whose name matches Name.
// Per child: Engine slot 0x68 GetInstanceName into an 8-byte scratch record, its first word
// read back, null replaced by "" (.text:0630e79f DAT_063997b0); `_stricmp(ChildName, Name) == 0`
// returns the child (.text:0630e7e4); else Found = FindChild(Child, Name) and advance. The loop
// tests `Found != -1` at its TOP (.text:0630e780), so after a successful recursive find one more
// sibling is fetched before returning -- reproduced literally. No null test of DALib::Engine or
// of the GetInstanceName result. Caller: CArchGroup::CArchGroup (the group's root instance).
long __cdecl FindChild(long Instance, char const* Name)
{
    long Found = -1;
    long Child = (long)DALib::Engine->GetRelatedInstance((void*)Instance, 1, G_InvalidEngineHandle);
    if (Child != -1)
    {
        do
        {
            if (Found != -1)
                return Found;
            FEngineName Info = {nullptr, 0};
            char const* ChildName = DALib::Engine->GetInstanceName(&Info, (void*)Child)->Text;
            if (ChildName == nullptr)
                ChildName = "";
            if (_stricmp(ChildName, Name) == 0)
                return Child;
            Found = FindChild(Child, Name);
            Child = (long)DALib::Engine->GetRelatedInstance((void*)Instance, 1, (void*)Child);
        } while (Child != -1);
    }
    return Found;
}

// Slot 53 is GetInstanceParent and slot 32 GetInstanceUserData -- which is where a CObject
// rides on its engine instance.

COMMON_API long __cdecl GetRoot(long Inst)
{
    long ParentInst;
    do
    {
        ParentInst = Inst;
        Inst = (long)DALib::Engine->GetInstanceParent((void*)ParentInst);
    } while (Inst != -1);
    return ParentInst;
}

COMMON_API CObject* __cdecl GetRoot(CObject const* Obj)
{
    long Inst = Obj->EngineInstanceId;
    if (Inst != -1)
    {
        long ParentInst;
        do
        {
            ParentInst = Inst;
            Inst = (long)DALib::Engine->GetInstanceParent((void*)ParentInst);
        } while (Inst != -1);
        return (CObject*)DALib::Engine->GetInstanceUserData((void*)ParentInst);
    }
    return const_cast<CObject*>(Obj);
}

COMMON_API void __cdecl HardpointToObject(long Inst, HardpointInfo& Info)
{
    // Common.dll.txt 0630ec40-0630ed97 (Ghidra's pseudocode is register-mangled here). The
    // mirror image of HardpointToWorld (common_hardpointsummary.cpp): converts Info, a WORLD
    // space frame, into Inst's own object-local space.
    // L = {Info.Orient, Info.Position}; T = Engine +0xa0 (no null checks); v =
    // TransposeTransform(T.Rotation, T.Translation) (3DMathEngine +0x38, the shared singleton);
    // Inv = {R^T built column-wise through the Matrix ctor FUN_06284f70, (-v.x, -v.y, -v.z)};
    // Out = MulTransform(Inv, L) (+0x1c); Info.Position = Out.Translation, Info.Orient =
    // Out.Rotation.
    Transform World;
    World.Rotation = Info.Orient;
    World.Translation = Info.Position;

    Transform const* const InstXform =
        (Transform const*)DALib::Engine->GetInstanceTransform((void*)Inst);

    I3DMathEngine* const Math = Acquire3DMathEngine();
    FVector Rotated;
    Math->TransposeTransform(Rotated, (FMatrix const&)InstXform->Rotation,
                             (FVector const&)InstXform->Translation);
    Matrix const& R = InstXform->Rotation;
    Transform Inverse;
    Inverse.Rotation = Matrix(R.E[0][0], R.E[1][0], R.E[2][0],
                              R.E[0][1], R.E[1][1], R.E[2][1],
                              R.E[0][2], R.E[1][2], R.E[2][2]);
    Inverse.Translation.x = -Rotated.X;
    Inverse.Translation.y = -Rotated.Y;
    Inverse.Translation.z = -Rotated.Z;

    FTransform Local;
    Acquire3DMathEngine()->MulTransform(Local, (FTransform const&)Inverse,
                                        (FTransform const&)World);
    Info.Position = (Vector const&)Local.Translation;
    Info.Orient = (Matrix const&)Local.Rotation;
}

namespace
{
    // Common.dll.txt 0x06303550 (FUN_06303550) is also the callback IHardpoint's
    // EnumerateHardpoints (slot 5) gets here: it touches only its second argument
    // (`MOV EAX,[ESP+8]; INC dword ptr [EAX]`). This wrapper has EnumerateHardpoints'
    // callback signature.
    void __cdecl NumHardpoints_CountCallback(char const* /*Name*/, void* Context)
    {
        int* const Count = (int*)Context;
        ++*Count;
    }
}

COMMON_API int __cdecl NumHardpoints(long Inst, bool bFlag)
{
    // Common.dll.txt 0630f070-0630f13a (Ghidra's pseudocode is corrupt here). For Inst, and
    // when bFlag is set also for every child from IEngine GetRelatedInstance (+0xd8) with
    // flag 0 (not CompoundInstanceAnalyze's flag 1): GetInstanceArchetype (+0x64), release
    // that handle at once (ReleaseHandle +0x3c; the value stays a valid lookup key), then
    // IHardpoint EnumerateHardpoints (+0x14) counting each hardpoint. The `TEST AL,AL` after
    // the first enumerate re-reads bFlag (ESP+8 at entry). No null test of DALib::Engine /
    // DALib::Hardpoint (.text:0630f071-0630f08a, 0630f09e-0630f0b4).
    int Count = 0;
    auto const CountHardpointsOn = [&](long Handle)
    {
        void* const Arch = DALib::Engine->GetInstanceArchetype((void*)Handle);
        DALib::Engine->ReleaseHandle(Arch);
        DALib::Hardpoint->EnumerateHardpoints(&NumHardpoints_CountCallback, Arch, &Count);
    };

    CountHardpointsOn(Inst);

    if (bFlag)
    {
        for (long Child = (long)DALib::Engine->GetRelatedInstance((void*)Inst, 0, G_InvalidEngineHandle);
             Child != -1;
             Child = (long)DALib::Engine->GetRelatedInstance((void*)Inst, 0, (void*)Child))
        {
            CountHardpointsOn(Child);
        }
    }

    return Count;
}

struct JointInfo;

namespace
{
    // Common.dll.txt 0630e7f0-0630e86a. Context is a VC6 std::list of {Param1, Param2,
    // JointInfo copy} records (HardpointPositionA builds it on its stack, 0630ede3-0630edfa:
    // allocator byte, head sentinel, size). JointEnumCallback is that list's push_back: the
    // record {Param1, Param2, *Joint as 0x5c raw bytes, zeroes for a null Joint} goes into a
    // new 0x6c-byte node linked before the sentinel (node->Next = head, node->Prev =
    // head->Prev or the node itself, head->Prev = node, node->Prev->Next = node), ++size,
    // return true. JointInfo's layout is never read, so it stays an opaque blob.
    struct JointEnumNode
    {
        JointEnumNode* Next;            // +0x00
        JointEnumNode* Prev;            // +0x04
        long Param1;                    // +0x08  the parent handle the engine passed
        long Param2;                    // +0x0c  the child handle
        unsigned char JointCopy[0x5c];  // +0x10
    };
    static_assert(sizeof(JointEnumNode) == 0x6c, "JointEnumNode sizeof 0x6c");

    struct JointEnumContext
    {
        unsigned char Allocator;        // +0x00  the list's allocator byte
        JointEnumNode* Head;            // +0x04  sentinel
        int Count;                      // +0x08
    };
} // namespace

COMMON_API bool __cdecl JointEnumCallback(long Param1, long Param2, struct JointInfo const* Joint, void* Context)
{
    unsigned char JointCopy[0x5c];
    if (Joint != nullptr)
        memcpy(JointCopy, Joint, sizeof(JointCopy));
    else
        memset(JointCopy, 0, sizeof(JointCopy));

    JointEnumContext* const Ctx = (JointEnumContext*)Context;
    JointEnumNode* const HeadNode = Ctx->Head;
    JointEnumNode* const OldPrev = HeadNode->Prev;
    JointEnumNode* const NewNode = new JointEnumNode;
    NewNode->Next = HeadNode;
    NewNode->Prev = (OldPrev != nullptr) ? OldPrev : NewNode;
    HeadNode->Prev = NewNode;
    NewNode->Prev->Next = NewNode;
    NewNode->Param1 = Param1;
    NewNode->Param2 = Param2;
    memcpy(NewNode->JointCopy, JointCopy, sizeof(JointCopy));

    ++Ctx->Count;
    return true;
}

bool __cdecl HardpointPositionA(long Instance, char const* HpName, Vector* OutPos, Matrix* OutOrient)
{
    // Common.dll.txt 0630eda0-0630ef2a. Engine +0x4c (IsCompoundArchetype) non-zero: collect
    // the children with Engine +0x54 (EnumerateArchetypeChildren) and JointEnumCallback into
    // a stack std::list, try HardpointPositionA on each child handle in order, the first hit
    // gives true; then the list is erased and its sentinel freed. Otherwise: look HpName up
    // on Instance itself (IHardpoint +0xc with {name, CRC32}); a miss gives false, a hit
    // copies the hardpoint's position (+0x04) / orientation (+0x10) to the non-null outputs
    // and gives true. Both are local to the hardpoint's archetype - no world transform.
    bool bFound = false;
    if (DALib::Engine->IsCompoundArchetype((void*)Instance) != 0)
    {
        JointEnumContext Children;
        Children.Allocator = (unsigned char)Instance;
        Children.Head = new JointEnumNode;
        Children.Head->Next = Children.Head;
        Children.Head->Prev = Children.Head;
        Children.Count = 0;
        // Retail passes JointEnumCallback itself; its exported signature differs from the
        // engine's callback type only in parameter and return types of the same width.
        DALib::Engine->EnumerateArchetypeChildren(
            (void*)Instance, (int(__cdecl*)(void*, void*, int, void*))&JointEnumCallback, &Children);

        for (JointEnumNode* Node = Children.Head->Next; Node != Children.Head; Node = Node->Next)
        {
            if (HardpointPositionA(Node->Param2, HpName, OutPos, OutOrient))
            {
                bFound = true;
                break;
            }
        }

        JointEnumNode* Node = Children.Head->Next;
        while (Node != Children.Head)
        {
            JointEnumNode* const NextNode = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            delete Node;
            --Children.Count;
            Node = NextNode;
        }
        delete Children.Head;
        return bFound;
    }

    FHardpointKey Key;
    Key.Name = HpName;
    Key.Crc = DACOM_CRC::GetCRC32(HpName);
    HardpointInfo Info;
    // Retail tests AL only.
    if ((unsigned char)DALib::Hardpoint->GetHardpoint((void*)Instance, &Key, &Info) == 0)
        return false;
    if (OutPos != nullptr)
        *OutPos = Info.Position;
    if (OutOrient != nullptr)
        *OutOrient = Info.Orient;
    return true;
}
