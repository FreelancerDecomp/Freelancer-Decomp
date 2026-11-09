// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"
#include "EngBase.h"
#include "readfile.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

#pragma comment(lib, "dacom.lib")

// Typed math calls preserve the caller's float buffers and their spill boundaries.
static GENRESULT MathMulMatrix(I3DMathEngine* Math, float* Dst, const float* A, const float* B)
{
    return Math->MulMatrix(*(FMatrix*)Dst, *(const FMatrix*)A, *(const FMatrix*)B);
}

static GENRESULT MathTransformMatrix(I3DMathEngine* Math, float* Dst, const float* A, const float* B)
{
    return Math->TransformMatrix(*(FVector*)Dst, *(const FMatrix*)A, *(const FVector*)B);
}

static GENRESULT MathMatrixToQuat(I3DMathEngine* Math, float* Dst, const float* A)
{
    return Math->MatrixToQuaternion(*(FQuaternion*)Dst, *(const FMatrix*)A);
}

static GENRESULT MathQuatToMatrix(I3DMathEngine* Math, float* Dst, const float* A)
{
    return Math->QuaternionToMatrix(*(FMatrix*)Dst, *(const FQuaternion*)A);
}

static GENRESULT MathQuatNlerp(I3DMathEngine* Math, float* Dst, const float* A, const float* B, float C)
{
    return Math->QuatNlerp(*(FQuaternion*)Dst, *(const FQuaternion*)A, *(const FQuaternion*)B, C);
}


static const int kFdumpNotice = 0x100003;
static const int kFdumpError = 0x100001;
static const int kFdumpWarning = 0x100002;
static const int kFdumpFatal = 0x100000;
static const char* const G_FdumpFmtNotice = "%s(%d) : NOTICE:General:%s";
static const char* const G_FdumpFmtError = "%s(%d) : ERROR:General:%s";
static const char* const G_FdumpFmtWarning = "%s(%d) : WARNING:General:%s";
// s_%s(%d)_:_FATAL:General:%s, pushed with 0x100000 (e.g. 06615567 / 0661556c).
static const char* const G_FdumpFmtFatal = "%s(%d) : FATAL:General:%s";
// .data:0662a208 (from the PE bytes; the listing truncates it): the __FILE__ that the
// "Unknown data type!" sites of IChannel::CreateTyped (06615a5f, line 0x44D) and
// CreateFromObject (06615c16, line 0x479) push.
static const char* const G_FdumpFileAnimComponent =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\AnimComponent.cpp";
// .data:0662a5c8 / .data:0662aac0 (from the PE bytes): the __FILE__ of every FDUMP site in
// engine.cpp (0661f96a..06622a5d) and HardPoint.cpp (06625d4d..06626dd8).
static const char* const G_FdumpFileEngine =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Engine\\engine.cpp";
static const char* const G_FdumpFileHardPoint =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Hardpoint\\HardPoint.cpp";
// .data:0662a0f0 (from the PE bytes): the __FILE__ the FDUMPs of FUN_06611b00 (0x176 / 0x189 /
// 0x193), FUN_06611e50 (0x1D8) and FUN_066124e0 (0x2F5 / 0x2FA) push.
static const char* const G_FdumpFileAnim =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\Anim.cpp";
// .data:0662a3c0 (from the PE bytes): the __FILE__ every FDUMP of FUN_0661db70
// (ChannelDecompressFrames) pushes.
static const char* const G_FdumpFileChannelArch =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Channel\\channel_arch.cpp";
static const char* const G_AnonymousFormat = "anonymous-%d";
static unsigned long G_AnonymousIndex = 0;
// .data:0662a51c / .data:0662a510 (PE bytes): the status word of the load_engine_components
// aggregation NOTICE (0661fb18 / 0661fb6c).
static const char* const G_StatusOk = "[OK]";
static const char* const G_StatusFailed = "[FAILED]";

// Local copy of DALib FFileSystemAggDesc (0x34). Do not include DALib.h.
struct FEngBaseFileSystemDesc
{
    unsigned long Size;
    const char* ComponentName;
    const char* BackendName;
    const char* DataPath;
    unsigned long Field10;
    unsigned long Field14;
    unsigned long Field18;
    unsigned long Field1C;
    unsigned long Field20;
    unsigned long Field24;
    unsigned long Field28;
    unsigned long Field2C;
    unsigned long Field30;
};

// FEngineObject holds a real std::map (the instance-root map), so only the facet offset is
// asserted; the retail byte layout is documented on the struct.
static_assert(offsetof(FEngineObject, SearchPath) == 0x04, "Engine.SearchPath");
static_assert(sizeof(FConstraintRecord) == 0x64, "constraint record 0x64");
static_assert(offsetof(FConstraintRecord, ChildPart) == 0x00, "constraint.ChildPart");
static_assert(offsetof(FConstraintRecord, ParentPart) == 0x04, "constraint.ParentPart");
static_assert(offsetof(FConstraintRecord, Payload) == 0x08, "constraint.Payload");
static_assert(sizeof(FArchetypeChild) == 0x48, "archetype child size");
static_assert(offsetof(FArchetypeChild, Crc) == 0x40, "child.Crc");
static_assert(offsetof(FArchetypeChild, Handle) == 0x44, "child.Handle");
static_assert(sizeof(FHardpointRecord) == 0x50, "hardpoint list value 0x50 (node 0x58)");
static_assert(offsetof(FEngineChildNode, Name) == 0x08, "child node Name");
static_assert(offsetof(FEngineChildNode, Object) == 0x38, "child node Object");
static_assert(sizeof(FEngineChildNode) == 0x3C, "child node size");
static_assert(offsetof(FAnimationObject, NextPlaybackId) == 0x10, "Animation.NextPlaybackId");
static_assert(offsetof(FAnimationObject, Playbacks) == 0x14, "Animation.Playbacks");
static_assert(offsetof(FAnimationObject, Channels) == 0x28, "Animation.Channels");
static_assert(offsetof(FAnimationObject, Controller) == 0x50, "Animation.Controller");
static_assert(offsetof(FAnimationObject, Inner) == 0x54, "Animation.Inner");
static_assert(sizeof(FAnimationObject) == G_AnimationObjectSize, "Animation object size");
static_assert(offsetof(FChannelArchetypeObject, RefCount) == 0x1C, "ChannelArchetype.RefCount");
static_assert(offsetof(FChannelArchetypeObject, FrameCount) == 0x04, "ChannelArchetype.FrameCount");
static_assert(offsetof(FChannelArchetypeObject, Duration) == 0x18, "ChannelArchetype.Duration");
static_assert(sizeof(FChannelArchetypeObject) == G_ChannelArchetypeObjectSize,
              "ChannelArchetype object size");
static_assert(sizeof(FAggDesc18) == 0x18, "AGGDESC 0x18");
static_assert(sizeof(FEngBaseFileSystemDesc) == 0x34, "FileSystem AGGDESC 0x34");
static_assert(sizeof(FEngineInstance) == G_EngineInstanceSize, "instance size");
static_assert(offsetof(FEngineInstance, Archetype) == 0x08, "instance.Archetype");
static_assert(offsetof(FEngineInstance, ParentArchetype) == 0x0C, "instance.ParentArchetype");
static_assert(offsetof(FEngineInstance, Parent) == 0x10, "instance.Parent");
static_assert(offsetof(FEngineInstance, NextSibling) == 0x14, "instance.NextSibling");
static_assert(offsetof(FEngineInstance, FirstChild) == 0x1C, "instance.FirstChild");
static_assert(offsetof(FEngineInstance, LastChild) == 0x20, "instance.LastChild");
static_assert(offsetof(FEngineInstance, UserData) == 0x28, "instance.UserData");
static_assert(offsetof(FEngineInstance, Matrix) == 0x2C, "instance.Matrix");
static_assert(offsetof(FEngineInstance, Relative) == 0x5C, "instance.Relative");
static_assert(offsetof(FEngineInstance, JointState) == 0xB8, "instance.JointState");
static_assert(offsetof(FEngineInstance, bWorldDirty) == 0xD4, "instance.bWorldDirty");
static_assert(offsetof(FEngineInstance, PartChild) == 0xE8, "instance.PartChild");
static_assert(sizeof(FInstanceTransform) == G_TransformObjectSize, "transform size");
static_assert(offsetof(FInstanceTransform, Matrix) == 0x04, "transform.Matrix");
static_assert(offsetof(FInstanceTransform, Extra) == 0x34, "transform.Extra");
static_assert(offsetof(FInstanceTransform, User) == 0x44, "transform.User");
static_assert(sizeof(FAggDescC) == 0xC, "AGGDESC 0xC");

struct FInnerIidEntry
{
    const char* Iid;
    long Offset;
};

// Preserve imported FDUMP formatting and call behavior.
void EngBaseFdump(int Flags, const char* File, int Line, const char* Kind, const char* Message)
{
    const char* Format = G_FdumpFmtNotice;
    if (Flags == kFdumpError)
        Format = G_FdumpFmtError;
    else if (Flags == kFdumpWarning)
        Format = G_FdumpFmtWarning;
    else if (Flags == kFdumpFatal)
        Format = G_FdumpFmtFatal;
    (void)Kind;
    FDUMP(Flags, Format, File, Line, Message);
}

// FUN_06611d80 (cdecl, variadic): _vsnprintf(Buffer, 0x2000, Format, args) and return Buffer.
static char* FormatFdumpMessage(char* Buffer, const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, 0x2000, Format, Args);
    va_end(Args);
    return Buffer;
}

bool EngBaseIidEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    return std::strcmp(Left, Right) == 0;
}

// Node names occupy 0x30 bytes; preserve the existing bounded-copy policy.
static void BuildNodeName(char* Buffer, const char* Source)
{
    if (Source == nullptr)
    {
        Buffer[0] = '\0';
        return;
    }
    const size_t Length = std::strlen(Source) + 1;
    std::memcpy(Buffer, Source, Length < 0x30 ? Length : 0x30);
}

// FUN_0661d700: XOR EAX,EAX / RET 4. Always 0; factories destroy the object if nonzero.
static int DACOM_API EngBaseAggregationHook(void* Desc)
{
    (void)Desc;
    return 0;
}



GENRESULT DACOM_API FDacomFactory::QueryInterface(const char* Iid, void** Out)
{
    if (Out != nullptr)
        *Out = nullptr;
    if (Iid == nullptr || Out == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    if (!EngBaseIidEqual(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = this;
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FDacomFactory::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FDacomFactory::Release()
{
    if (RefCount != 0)
        RefCount -= 1;
    const unsigned long Count = RefCount;
    if (Count == 0)
    {
        RefCount = 1;
        std::free(this);
        return 0;
    }
    return Count;
}

static GENRESULT DACOM_API StubRet4(void* This)
{
    (void)This;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet8(void* This, void* A)
{
    (void)This;
    (void)A;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRetC(void* This, void* A, void* B)
{
    (void)This;
    (void)A;
    (void)B;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet10(void* This, void* A, void* B, void* C)
{
    (void)This;
    (void)A;
    (void)B;
    (void)C;
    return GR_GENERIC;
}

// LAB_06626cd0 / IEC+0x20 RET 0xC: XOR EAX.
// formerly HardpointIecStubRetC
static GENRESULT DACOM_API NoOpHardpointInstance(void* Nested, void* A, void* B)
{
    (void)Nested;
    (void)A;
    (void)B;
    return GR_OK;
}

// LAB_06626cf0 / IEC+0x1C and +0x2C RET 0x10: OR EAX,-1.
// formerly HardpointIecStubRet10
static GENRESULT DACOM_API RejectHardpointQuery(void* Nested, void* A, const void* ArgIid, void* C)
{
    (void)Nested;
    (void)A;
    (void)ArgIid;
    (void)C;
    return (GENRESULT)(-1);
}

static GENRESULT DACOM_API StubRet14(void* This, void* A, void* B, void* C, void* D)
{
    (void)This;
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet18(void* This, void* A, void* B, void* C, void* D, void* E)
{
    (void)This;
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    (void)E;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet1C(void* This, void* A, void* B, void* C, void* D, void* E, void* F)
{
    (void)This;
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    (void)E;
    (void)F;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet24(void* This, void* A, void* B, void* C, void* D, void* E, void* F,
                                    void* G, void* H)
{
    (void)This;
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    (void)E;
    (void)F;
    (void)G;
    (void)H;
    return GR_GENERIC;
}

// LAB_06627ef0 (RET 4), ICF-shared by six inner/factory vtables: INC [this+4] and return it.
// No check of This (the port's null guard is gone, 2026-09-30).
unsigned long DACOM_API FAnimationInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FHardpointInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// _strdup + _strlwr, the pair every hardpoint-record builder calls through the MSVCRT imports.
static char* DuplicateLoweredName(char const* Name)
{
    char* Copy = _strdup(Name);
    _strlwr(Copy);
    return Copy;
}

// FUN_06626310 (create_archetype, 0662604b) / FUN_066265e0 behind DAT_0662ace4 (add_hardpoint,
FHardpointRecord::FHardpointRecord(unsigned long InCrc, char const* InName)
    : Crc(InCrc)
    , Name(DuplicateLoweredName(InName != nullptr ? InName : ""))
{
    std::memset(Data, 0, sizeof(Data));
}

// FUN_06627f70 / the element loops of FUN_06627d60 and FUN_06627fe0: Crc and blob copied, the
// name duplicated + lowercased only when the source has one.
FHardpointRecord::FHardpointRecord(FHardpointRecord const& Other)
    : Crc(Other.Crc)
    , Name(nullptr)
{
    std::memcpy(Data, Other.Data, sizeof(Data));
    if (Other.Name != nullptr)
        Name = DuplicateLoweredName(Other.Name);
}

// FUN_066280b0's per-node step: free the name when there is one.
FHardpointRecord::~FHardpointRecord()
{
    if (Name != nullptr)
        std::free(Name);
}

// FUN_06625c70 (ctor): the map is set up empty, +0x20 / +0x24 / +0x08 / +0x0C zeroed and the
// cache byte cleared. Called on calloc'd memory through placement new.
static void HardpointConstruct(FHardpointObject* Object)
{
    new (Object) FHardpointObject();
    Object->LookupCount = 0;
    Object->CacheHits = 0;
    Object->Field_0x24 = 0;
    Object->bCacheValid = 0;
}

// FUN_06625d00 (dtor, thiscall). Walks the map from begin() in ascending key order and, for
static void HardpointDestruct(FHardpointObject* Object)
{
    for (FHardpointMap::iterator It = Object->Archetypes.begin(); It != Object->Archetypes.end();
         ++It)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpNotice, G_FdumpFileHardPoint, 0xC8, "NOTICE",
                     FormatFdumpMessage(Message,
                                        "Hardpoint: dtor: Archetype %d has dangling hardpoints\n",
                                        It->first));
    }
    Object->Engine = nullptr;
    Object->~FHardpointObject();
}

static void DestroyAnimationLists(FAnimationObject* Object);

// LAB_06627f00 (RET 4): count > 0 (JBE, unsigned) -> count--; count != 0 -> return it. At zero:
// count = 1, and when the object pointer is non-null run the dtor FUN_06625d00 and free it;
// return 0.
unsigned long DACOM_API FHardpointInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FHardpointObject* Owner = Object;
    if (Owner != nullptr)
    {
        HardpointDestruct(Owner);
        std::free(Owner);
    }
    return 0;
}

// LAB_0661a940 (RET 4): count > 0 (JBE, unsigned) -> count--; count != 0 -> return it. At
// zero: count = 1, and when the object pointer (+8) is non-null run the dtor FUN_06616d90 and
// free it; return 0.
unsigned long DACOM_API FAnimationInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FAnimationObject* Owner = Object;
    if (Owner != nullptr)
    {
        DestroyAnimationLists(Owner);
        std::free(Owner);
    }
    return 0;
}

// The {iid, offset} walk both inner unknowns inline (LAB_0661a880 over 06629478,
static GENRESULT InnerQueryTable(void* Object, const char* Iid, void** Out,
                                 const FInnerIidEntry* Table)
{
    for (int Index = 0; Table[Index].Iid != nullptr; ++Index)
    {
        if (std::strcmp(Table[Index].Iid, Iid) != 0)
            continue;
        unsigned char* Base = (unsigned char*)Object;
        const long Offset = Table[Index].Offset;
        IDAComponent* Interface = nullptr;
        if (Offset < 0)
            std::memcpy(&Interface, Base + ((unsigned long)Offset & 0x7FFFFFFFu),
                        sizeof(Interface));
        else
            Interface = (IDAComponent*)(Base + Offset);
        Interface->AddRef();
        *Out = Interface;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// Inner QI table @ 06629478 (PE bytes): {IAnimation2, 0}, {IAnimation, 0}, {IChannel, 8},
// {IRootData, 0xC}, {IAggregateComponent, 4}, then a null iid.
static const FInnerIidEntry G_AnimationInnerIids[] = {
    {Iids::Animation2, 0},
    {Iids::Animation, 0},
    {Iids::Channel, 0x08},
    {Iids::RootData, 0x0C},
    {Iids::AggregateComponent, 0x04},
    {nullptr, 0},
};

// Inner QI table @ 06629844 (PE bytes): {IHardpoint, 0}, {IEngineComponent, 4},
// {IAggregateComponent, 4}, then a null iid. IAggregateComponent answers with the IEC facet.
static const FInnerIidEntry G_HardpointInnerIids[] = {
    {Iids::Hardpoint, 0},
    {Iids::EngineComponent, 0x04},
    {Iids::AggregateComponent, 0x04},
    {nullptr, 0},
};

// LAB_0661a880 (inner QI, RET 0xC) over 06629478; the facet offsets are added to the object
// pointer at inner +8.
GENRESULT DACOM_API FAnimationInner::QueryInterface(const char* Iid, void** Out)
{
    return InnerQueryTable(Object, Iid, Out, G_AnimationInnerIids);
}

// LAB_06627e30 (inner QI, RET 0xC) over 06629844.
GENRESULT DACOM_API FHardpointInner::QueryInterface(const char* Iid, void** Out)
{
    return InnerQueryTable(Object, Iid, Out, G_HardpointInnerIids);
}

// EngBase plumbs the FileSystem component around as void*; this is the one place that
// states what it really is, so every call below goes through ReadFile's published ABI.
static IFileSystem* AsFileSystem(void* FileSystem)
{
    return (IFileSystem*)FileSystem;
}

static int FileSystemOpenNode(void* FileSystem, const char* Name);
static int FileSystemFindFirst(void* FileSystem, const char* Pattern, WIN32_FIND_DATAA* FindData);
static int FileSystemFindNext(void* FileSystem, int FindHandle, WIN32_FIND_DATAA* FindData);
static void FileSystemFindClose(void* FileSystem, int FindHandle);

// LAB_0661a980 / LAB_0661a990 / LAB_0661a9a0 (the IAnimation2 slots 0-2, also reached from the
// three facet thunks after SUB this): replace the this argument with [this+0x50] and jump
// through its vtable +0 / +4 / +8. No checks.
static GENRESULT DACOM_API AnimationQueryInterface(FAnimationObject* This, const char* Iid,
                                                  void** Out)
{
    return This->Controller->QueryInterface(Iid, Out);
}

static unsigned long DACOM_API AnimationAddRef(FAnimationObject* This)
{
    return This->Controller->AddRef();
}

static unsigned long DACOM_API AnimationRelease(FAnimationObject* This)
{
    return This->Controller->Release();
}

static FAnimationObject* AnimationFromNested(void* Nested, unsigned long Offset)
{
    return (FAnimationObject*)((unsigned char*)Nested - Offset);
}

// Shared engine cache; Engine2 has its own QI output slot.
FEngineObject* G_AnimEngine = nullptr;
static FEngineObject* G_AnimEngine2 = nullptr;

static GENRESULT DACOM_API AggregateInitialize(void* Nested)
{
    FAnimationObject* Object = AnimationFromNested(Nested, 4);
    GENRESULT Result =
        AnimationQueryInterface(Object, Iids::Engine, (void**)(&G_AnimEngine));
    if (Result != 0)
        return Result;
    G_AnimEngine->Release();
    Result =
        AnimationQueryInterface(Object, Iids::Engine2, (void**)(&G_AnimEngine2));
    if (Result != 0)
        return Result;
    G_AnimEngine2->Release();
    return GR_OK;
}

static FAnimScript* const G_InvalidAnimScript =
    (FAnimScript*)((ptrdiff_t)(-1));

struct FAnimHandleNode
{
    FAnimHandleNode* Next;
    unsigned long Key;
};

static FAnimHandleNode* G_AnimHandleHead = nullptr;

static GENRESULT DACOM_API ChannelArchetypeLoad(FChannelArchetypeObject* This, void* FileSystem);
static GENRESULT DACOM_API ChannelArchetypeGetDuration(FChannelArchetypeObject* This, float* Out);
static GENRESULT DACOM_API ChannelArchetypeGetFlags(FChannelArchetypeObject* This,
                                                   unsigned long* Out);
static GENRESULT DACOM_API ChannelArchetypeSample(FChannelArchetypeObject* This, float Time,
                                                 const unsigned char** OutA,
                                                 const unsigned char** OutB, float* Alpha);
static GENRESULT DACOM_API ChannelArchetypeSamplePair(FChannelArchetypeObject* This, float Time,
                                                     const unsigned char** OutLo,
                                                     const unsigned char** OutHi, float* TimeLo,
                                                     float* TimeHi, float* Alpha);
static GENRESULT DACOM_API ChannelArchetypeGetFrame(FChannelArchetypeObject* This,
                                                   unsigned char bLast, const unsigned char** Out);
static GENRESULT DACOM_API ChannelArchetypeCollectRange(FChannelArchetypeObject* This, float Time0,
                                                       float Time1, float* OutKeys,
                                                       unsigned long Capacity, unsigned long* Count);
static GENRESULT DACOM_API ChannelArchetypeCollectAt(FChannelArchetypeObject* This, float Time,
                                                    float* OutKeys, unsigned long Capacity,
                                                    unsigned long* Count);
static void ChannelDecompressFrames(FChannelArchetypeObject* Channel);
static void FillFileSystemDesc(FEngBaseFileSystemDesc* Desc, const char* Path, const char* Backend);
static unsigned long DACOM_API ChannelArchetypeAddRef(FChannelArchetypeObject* This);
static unsigned long DACOM_API ChannelArchetypeRelease(FChannelArchetypeObject* This);
static void ChannelArchetypeInnerCtor(FChannelArchetypeObject* This);
static void ChannelArchetypeTeardown(FChannelArchetypeObject* This);
static void LoadAnimScriptMaps(FAnimNameEntry* Entry, const char* Name, void* FileSystem);
static FAnimNameEntry* FindAnimName(FAnimScript* Script, unsigned long Crc);
static unsigned char FindPlaybackRecord(FAnimationObject* Animation, unsigned long Key,
                                        FAnimPlayback** Out);
static void FreePlaybackTyped(FAnimPlayback* Playback);
static void ClonePlaybackTypedChannels(FAnimPlayback* Playback, FAnimNameEntry* Entry);
static unsigned long PlaybackFilterTyped(FAnimPlayback* Playback, IChannel* Filter);
static unsigned long PlaybackHasTyped(FAnimPlayback* Playback, void* Object);
static unsigned long PlaybackTouchMappedTyped(FAnimPlayback* Playback, void* ArgA, void* ArgB);
static void BindPlaybackTracks(FAnimPlayback* Playback, FEngineInstance* Root, void* ArgD,
                               void* ArgE);
static void PlaybackStopTyped(FAnimPlayback* Playback);
static void PlaybackApplyTyped(FAnimPlayback* Playback);
static unsigned long PlaybackEvalTyped(FAnimPlayback* Playback, unsigned long Flags, float Start,
                                      float Speed, unsigned long P3, unsigned long P4,
                                      unsigned long P5, unsigned long P6);
static unsigned long PlaybackEvalStoredTyped(FAnimPlayback* Playback);
static FTypedChannel* PlaybackFindTypedByHeader(FAnimPlayback* Playback, const FAnimBindOut* Header);
static void PlaybackSetTypedTime(FAnimPlayback* Playback, float Time);
static float PlaybackStoredDuration(FAnimPlayback* Playback);
static unsigned long DACOM_API AnimationPlayScript(FAnimationObject* This, FAnimScript* Script,
                                                  FEngineInstance* Root, const char* Name,
                                                  void* ArgD, void* ArgE);
static unsigned long DACOM_API AnimationEvalPlayback(FAnimationObject* This, unsigned long PlaybackId,
                                                    unsigned long Flags, float Time, float Scale,
                                                    void* UnusedA, void* UnusedB, void* UnusedC,
                                                    void* UnusedD);
static unsigned long DACOM_API AnimationEvalStored(FAnimationObject* This, unsigned long PlaybackId);
static unsigned long DACOM_API AnimationStopPlayback(FAnimationObject* This, unsigned long PlaybackId);
static unsigned long DACOM_API ChannelPlay(void* ChannelThis, unsigned long Key, float* Time);
static unsigned char DACOM_API RootDataEval(void* RootThis, unsigned long PlaybackId,
                                           void* TouchArg, unsigned long Flags, float Time,
                                           float Scale, void* Arg6, void* Arg7, float Arg8);

static void UnregisterAnimHandle(unsigned long Key);

static bool IsValidAnimScript(FAnimScript* Script)
{
    return Script != G_InvalidAnimScript && Script != nullptr;
}

// formerly Slot00
unsigned long DACOM_API FAnimScript::RejectOperation(void* ArgA, void* ArgB)
{
    (void)ArgA;
    (void)ArgB;
    return 0xFFFFFFFF;
}

unsigned long DACOM_API FAnimScript::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FAnimScript::Release()
{
    RefCount -= 1;
    const unsigned long Left = RefCount;
    if (Left != 0)
        return Left;
    delete this;
    return 0;
}

FAnimTrack::FAnimTrack(const FAnimTrack& Other)
    : Channel(Other.Channel)
    , ParentCrc(Other.ParentCrc)
    , ChildCrc(Other.ChildCrc)
    , ParentName(nullptr)
    , ChildName(nullptr)
{
    if (Channel != nullptr)
        Channel->AddRef();
    if (Other.ParentName != nullptr)
        ParentName = _strdup(Other.ParentName);
    if (Other.ChildName != nullptr)
        ChildName = _strdup(Other.ChildName);
}

FAnimTrack::~FAnimTrack()
{
    if (ParentName != nullptr)
        std::free(ParentName);
    if (ChildName != nullptr)
        std::free(ChildName);
    if (Channel != nullptr)
    {
        Channel->Release();
        Channel = nullptr;
    }
}

FAnimNameEntry::~FAnimNameEntry()
{
    std::free(Name);
    std::vector<FAnimTrack>().swap(Tracks);
    if (LongestChannel != nullptr)
    {
        LongestChannel->Release();
        LongestChannel = nullptr;
    }
}

static FAnimScript* AllocAnimScript()
{
    return new (std::nothrow) FAnimScript();
}

static FAnimNameEntry* InsertAnimName(FAnimScript* Script, unsigned long Crc)
{
    return &Script->Names[Crc];
}

static FTypedChannel* FindTypedChannel(FAnimationObject* Animation, unsigned long Key)
{
    if (Key == 0xFFFFFFFF)
        return nullptr;
    FTypedChannel* Wanted = (FTypedChannel*)((size_t)Key);
    FTypedChannelSet::iterator It = Animation->Channels->find(Wanted);
    if (It == Animation->Channels->end())
        return nullptr;
    return *It;
}

static void DestroyAnimationLists(FAnimationObject* Object)
{

    while (G_AnimHandleHead != nullptr)
    {
        FAnimHandleNode* Next = G_AnimHandleHead->Next;
        std::free(G_AnimHandleHead);
        G_AnimHandleHead = Next;
    }

    MixerDestroyAll(Object);
    delete Object->Channels;
    Object->Channels = nullptr;

    delete Object->Playbacks;
    Object->Playbacks = nullptr;
}

static unsigned long G_JointMapLength = 0;
static unsigned long G_ObjectMapLength = 0;
static unsigned long G_EventMapLength = 0;

static void StoreAnimMapNameLengths()
{
    G_JointMapLength = (unsigned long)(std::strlen(G_NameJointMap));
    G_ObjectMapLength = (unsigned long)(std::strlen(G_NameObjectMap));
    G_EventMapLength = (unsigned long)(std::strlen(G_NameEventMap));
}

static unsigned long LoadAnimScriptNames(FAnimScript* Script, void* FileSystem)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameScript) == 0)
        return 0;
    StoreAnimMapNameLengths();
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar,
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            if (FindData.cFileName[0] == '.')
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.Field30 = (unsigned long)FindHandle;
            void* NestedFs = nullptr;
            Fs->CreateInstance((AGGDESC*)(&Desc), &NestedFs);
            if (((FindData.dwFileAttributes >> 4) & 1) != 0)
            {
                FAnimNameEntry* Entry =
                    InsertAnimName(Script, DACOM_CRC::GetCRC32(FindData.cFileName));
                LoadAnimScriptMaps(Entry, FindData.cFileName, NestedFs);
            }
            if (NestedFs != nullptr)
                ((IDAComponent*)NestedFs)->Release();
        } while (Fs->FindNext((unsigned long)FindHandle,
                            &FindData) != 0);
        Fs->GetFileSystemError();
        Fs->FindClose((unsigned long)FindHandle);
    }
    Fs->OpenNode(G_NameParentDot);
    return 0;
}

static unsigned long LoadAnimScriptsByNameList(FAnimScript* Script, void* FileSystem,
                                              const char** Names, int Count)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameScript) == 0)
        return 0;
    StoreAnimMapNameLengths();
    for (int Index = 0; Index < Count; ++Index)
    {
        const char* Name = Names[Index];
        if (Name == nullptr)
        {
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x176, "WARNING",
                         "Null name in animation sub set. Aborting subset load.\n");
            break;
        }
        char Message[0x2000];
        if ((Fs->GetPathAttributes(Name) & 0x10) == 0)
        {
            FormatFdumpMessage(Message,"Unknown or old style animation \"%s\". Skipping.\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x193, "WARNING", Message);
            continue;
        }
        if (FindAnimName(Script, DACOM_CRC::GetCRC32(Name)) != nullptr)
            continue;
        FEngBaseFileSystemDesc Desc;
        FillFileSystemDesc(&Desc, Name, nullptr);
        void* NestedFs = nullptr;
        if (Fs->CreateInstance((AGGDESC*)(&Desc), &NestedFs) == 0)
        {
            FAnimNameEntry* Entry = InsertAnimName(Script, DACOM_CRC::GetCRC32(Name));
            LoadAnimScriptMaps(Entry, Name, NestedFs);
        }
        else
        {
            FormatFdumpMessage(Message,"Failed to open animation \"%s\". Skipping.\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x189, "WARNING", Message);
        }
        if (NestedFs != nullptr)
            ((IDAComponent*)NestedFs)->Release();
    }
    Fs->OpenNode(G_NameParentDot);
    return 0;
}

static FAnimScript* DACOM_API AnimationOpenScript(FAnimationObject* This, void* FileSystem)
{
    (void)This;
    FAnimScript* Script = AllocAnimScript();
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameAnimation) == 0)
    {
        Script->Release();
        return G_InvalidAnimScript;
    }
    LoadAnimScriptNames(Script, FileSystem);
    Fs->OpenNode(G_NameParentDot);
    return Script;
}

static GENRESULT DACOM_API AnimationCloseScript(FAnimationObject* This, FAnimScript* Script)
{

    (void)This;
    if (!IsValidAnimScript(Script))
        return (GENRESULT)((size_t)Script);
    return (GENRESULT)(Script->Release());
}

static FAnimScript* DACOM_API AnimationCreateEmptyScript(FAnimationObject* This)
{

    (void)This;
    return AllocAnimScript();
}

static GENRESULT DACOM_API AnimationEnumScriptNames(FAnimationObject* This, void* Callback,
                                                   FAnimScript* Script, void* Context)
{

    (void)This;
    if (Callback == nullptr || !IsValidAnimScript(Script))
        return GR_OK;
    using FnCb = void(__cdecl*)(const char*, void*);
    FnCb Enum = (FnCb)Callback;
    FAnimNameMap::iterator It = Script->Names.begin();
    while (It != Script->Names.end())
    {
        Enum(It->second.Name, Context);
        ++It;
    }
    return GR_OK;
}

static GENRESULT DACOM_API AnimationFindScriptName(FAnimationObject* This, void* Callback,
                                                  FAnimScript* Script, const char* Name,
                                                  void* Context)
{
    (void)This;
    const GENRESULT Miss = (GENRESULT)0xFFFFFFFE;
    if (Callback == nullptr || !IsValidAnimScript(Script))
        return Miss;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return Miss;
    using FnCb = void(__cdecl*)(void*, void*);
    FnCb Enum = (FnCb)Callback;
    std::vector<FAnimTrack>::iterator Track = Entry->Tracks.begin();
    while (Track != Entry->Tracks.end())
    {
        if (Track->Channel != nullptr)
        {
            ChannelArchetypeAddRef(Track->Channel);
            Enum((IChannelArchetype*)(Track->Channel), Context);
        }
        ++Track;
    }
    return 0;
}

static unsigned char FindPlaybackRecord(FAnimationObject* Animation, unsigned long Key,
                                        FAnimPlayback** Out)
{
    FAnimPlaybackMap::iterator It = Animation->Playbacks->find((long)Key);
    if (It == Animation->Playbacks->end())
        return 0;
    *Out = &It->second;
    return 1;
}

static GENRESULT DACOM_API AnimationDestroyScript(FAnimationObject* This, unsigned long PlaybackId)
{
    FAnimPlaybackMap::iterator It = This->Playbacks->find((long)PlaybackId);
    if (It == This->Playbacks->end())
        return GR_OK;
    FAnimPlayback* Playback = &It->second;
    FAnimScript* Script = Playback->Script;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        MixerUnbindTyped(This, *Cursor);
        This->Channels->erase(*Cursor);
        ++Cursor;
    }
    FreePlaybackTyped(Playback);
    This->Playbacks->erase(It);
    Script->Release();
    return GR_OK;
}

static float DACOM_API AnimationGetScriptDuration(FAnimationObject* This, unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0.0f;
    float Duration = -1.0f;
    Playback->Name->LongestChannel->GetDuration(&Duration);
    return Duration;
}

static float DACOM_API AnimationGetNamedDuration(FAnimationObject* This, FAnimScript* Script,
                                               const char* Name)
{
    (void)This;
    if (!IsValidAnimScript(Script))
        return -1.0f;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return -1.0f;
    float Duration = -1.0f;
    Entry->LongestChannel->GetDuration(&Duration);
    return Duration;
}

static float DACOM_API AnimationGetStoredDuration(FAnimationObject* This, unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return -1.0f;
    return PlaybackStoredDuration(Playback);
}

static unsigned long DACOM_API AnimationHasScriptName(FAnimationObject* This, FAnimScript* Script,
                                                    const char* Name)
{
    (void)This;
    if (!IsValidAnimScript(Script) || Name == nullptr)
        return 0xFFFFFFFF;

    const unsigned long Crc = DACOM_CRC::GetCRC32(Name);
    if (Script->Names.find(Crc) != Script->Names.end())
        return 0;
    return 0xFFFFFFFF;
}

static unsigned long DACOM_API AnimationGetScriptNameCount(FAnimationObject* This,
                                                         FAnimScript* Script,
                                                         unsigned long* Out)
{

    (void)This;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    *Out = (unsigned long)(Script->Names.size());
    return 0;
}

static FAnimNameEntry* FindAnimName(FAnimScript* Script, unsigned long Crc)
{
    FAnimNameMap::iterator It = Script->Names.find(Crc);
    if (It == Script->Names.end())
        return nullptr;
    return &It->second;
}

static bool RemoveAnimNameByCrc(FAnimScript* Script, unsigned long Crc)
{
    return Script->Names.erase(Crc) != 0;
}

static bool FindAnimHandle(unsigned long Key)
{
    FAnimHandleNode* Node = G_AnimHandleHead;
    while (Node != nullptr)
    {
        if (Node->Key == Key)
            return true;
        Node = Node->Next;
    }
    return false;
}

static unsigned long RegisterAnimHandle(unsigned long Key)
{
    if (Key == 0xFFFFFFFF)
        return 0xFFFFFFFF;
    if (FindAnimHandle(Key))
        return 0;
    FAnimHandleNode* Node = (FAnimHandleNode*)(std::calloc(1, sizeof(FAnimHandleNode)));
    if (Node == nullptr)
        return 0xFFFFFFFF;
    Node->Key = Key;
    Node->Next = G_AnimHandleHead;
    G_AnimHandleHead = Node;
    return 0;
}

static void UnregisterAnimHandle(unsigned long Key)
{
    FAnimHandleNode* Prev = nullptr;
    FAnimHandleNode* Node = G_AnimHandleHead;
    while (Node != nullptr)
    {
        if (Node->Key == Key)
        {
            if (Prev != nullptr)
                Prev->Next = Node->Next;
            else
                G_AnimHandleHead = Node->Next;
            std::free(Node);
            return;
        }
        Prev = Node;
        Node = Node->Next;
    }
}

static unsigned long DACOM_API AnimationFilterObjects(FAnimationObject* This, unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    IChannel* ChannelFacet = (IChannel*)This;
    return PlaybackFilterTyped(Playback, ChannelFacet);
}

static unsigned long DACOM_API AnimationHasObject(FAnimationObject* This, unsigned long PlaybackId,
                                                 void* Object)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    return PlaybackHasTyped(Playback, Object);
}

static unsigned long DACOM_API AnimationGetFirstInstanceField(FAnimationObject* This,
                                                            unsigned long PlaybackId,
                                                            unsigned long* Out)
{
    FAnimPlaybackMap::iterator It = This->Playbacks->find((long)PlaybackId);
    if (It == This->Playbacks->end())
        return 0;
    FAnimNameMap& Names = It->second.Script->Names;
    FAnimNameMap::iterator Cursor = Names.begin();
    while (Cursor != Names.end())
    {
        if (&Cursor->second == It->second.Name)
        {
            *Out = (unsigned long)((size_t)(Cursor->second.Name));
            return 1;
        }
        ++Cursor;
    }
    return 0;
}

static unsigned long DACOM_API AnimationGetDurationIfNamed(FAnimationObject* This,
                                                          unsigned long PlaybackId, float* Out)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    if (Playback->Name == nullptr)
        return 0;
    if (Playback->Name->bHasRootHeight == 0)
        return 0;
    *Out = Playback->Name->RootHeight;
    return 1;
}

static unsigned long DACOM_API AnimationGetNamedDurationOut(FAnimationObject* This,
                                                           FAnimScript* Script, const char* Name,
                                                           float* Out)
{
    (void)This;
    if (!IsValidAnimScript(Script))
        return 0;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return 0;
    if (Entry->bHasRootHeight == 0)
        return 0;
    *Out = Entry->RootHeight;
    return 1;
}

static unsigned long DACOM_API AnimationFindObject(FAnimationObject* This, unsigned long PlaybackId,
                                                  void* Query)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    return (unsigned long)(
        PlaybackFindTypedByHeader(Playback, (const FAnimBindOut*)Query));
}

static unsigned long DACOM_API AnimationAddScriptNames(FAnimationObject* This, FAnimScript* Script,
                                                     void* FileSystem, const char** Names,
                                                     int Count)
{
    (void)This;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameAnimation) == 0)
        return 0xFFFFFFFF;
    const unsigned long Result = LoadAnimScriptsByNameList(Script, FileSystem, Names, Count);
    Fs->OpenNode(G_NameParentDot);
    return Result;
}

static unsigned long DACOM_API AnimationRemoveScriptNames(FAnimationObject* This, FAnimScript* Script,
                                                        const char** Names, int Count)
{
    (void)This;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    for (int Index = 0; Index < Count; ++Index)
    {
        const char* Name = Names[Index];
        if (Name == nullptr)
            continue;
        if (!RemoveAnimNameByCrc(Script, DACOM_CRC::GetCRC32(Name)))
        {
            char Message[0x2000];
            FormatFdumpMessage(Message,"Failed to remove animation \"%s\".\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x1D8, "WARNING", Message);
        }
    }
    return 0;
}

static unsigned long DACOM_API AnimationEnumFsScripts(FAnimationObject* This, void* Callback,
                                                    void* FileSystem, void* Context)
{
    (void)This;

    if (FileSystemOpenNode(FileSystem, G_NameAnimation) == 0)
        return 0xFFFFFFFF;

    if (FileSystemOpenNode(FileSystem, G_NameScript) == 0)
    {
        FileSystemOpenNode(FileSystem, G_NameParentDot);
        return 0xFFFFFFFF;
    }

    WIN32_FIND_DATAA FindData = {};
    const int FindHandle = FileSystemFindFirst(FileSystem, G_NameStar, &FindData);
    if (FindHandle != -1)
    {
        using FnCb = void(__cdecl*)(const char*, void*);
        FnCb Enum = (FnCb)Callback;
        do
        {

            if (FindData.cFileName[0] != '.' &&
                (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                Enum(FindData.cFileName, Context);
            }
        } while (FileSystemFindNext(FileSystem, FindHandle, &FindData) != 0);

        AsFileSystem(FileSystem)->GetFileSystemError();
        FileSystemFindClose(FileSystem, FindHandle);
    }
    FileSystemOpenNode(FileSystem, G_NameParentDot);
    FileSystemOpenNode(FileSystem, G_NameParentDot);
    return 0;
}

static unsigned long DACOM_API AnimationEnumObjects(FAnimationObject* This, void* Callback,
                                                   unsigned long PlaybackId, void* Context)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    using FnEnum = unsigned char(__cdecl*)(void* Object, void* User);
    FnEnum Enum = (FnEnum)Callback;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        if (Enum(*Cursor, Context) == 0)
            break;
        ++Cursor;
    }
    return 0;
}

static unsigned long DACOM_API AnimationRegisterHandle(FAnimationObject* This, unsigned long Key)
{
    (void)This;
    return RegisterAnimHandle(Key);
}

static unsigned long DACOM_API AnimationHasHandle(FAnimationObject* This, unsigned long Key,
                                                 unsigned char* Out)
{
    (void)This;
    if (Out != nullptr)
        *Out = FindAnimHandle(Key) ? 1 : 0;
    return 0;
}

static unsigned long DACOM_API AnimationHasAnyHandle(FAnimationObject* This, unsigned long Key)
{
    (void)This;
    if (!FindAnimHandle(Key))
        return 0xFFFFFFFF;
    UnregisterAnimHandle(Key);
    return 0;
}

static bool IsTypedChannelType(unsigned long Type)
{
    return Type == 1 || Type == 2 || Type == 4 || Type == 6 || Type == 8;
}

static unsigned long TypedKindFromType(unsigned long Type)
{
    if (Type == 1 || Type == 2)
        return G_MixKindScalar;
    if (Type == 3)
        return 2;
    if (Type == 4)
        return G_MixKindQuat;
    if (Type == 5)
        return G_MixKindVec3;
    if (Type == 6 || Type == 8)
        return G_MixKindPosQuat;
    return G_MixKindScalar;
}

static unsigned long DACOM_API TypedSetTime(FTypedChannel* This, float Time);

static unsigned long DACOM_API TypedBind(FTypedChannel* This, unsigned long* Header)
{
    const float* HeaderF = (const float*)Header;
    This->LoopFlags = Header[1];
    This->Weight = HeaderF[2];
    This->Scale = HeaderF[4];
    This->Field_0x30 = HeaderF[5];
    This->SetTime(HeaderF[0]);
    This->PlayFlags &= ~3UL;
    return This->PlayFlags;
}

static void TypedCopyVec3(float* Dest, const float* Source)
{
    Dest[0] = Source[0];
    Dest[1] = Source[1];
    Dest[2] = Source[2];
}

static void TypedStoreIdentityMatrix(float* Matrix)
{
    Matrix[0] = 1.0f;
    Matrix[1] = 0.0f;
    Matrix[2] = 0.0f;
    Matrix[3] = 0.0f;
    Matrix[4] = 1.0f;
    Matrix[5] = 0.0f;
    Matrix[6] = 0.0f;
    Matrix[7] = 0.0f;
    Matrix[8] = 1.0f;
}

static unsigned long DACOM_API TypedBindType6(FPosQuatTypedChannel* This, unsigned long* Header)
{
    TypedBind(This, Header);
    if (This->Kind != 1)
    {
        TypedStoreIdentityMatrix(This->Matrix);
        This->Delta[0] = 0.0f;
        This->Delta[1] = 0.0f;
        This->Delta[2] = 0.0f;
        return 0;
    }
    const float* HeaderF = (const float*)Header;
    const float* Block = (const float*)((size_t)(Header[6]));
    FEngineInstance* Instance = (FEngineInstance*)((size_t)(This->Field14));
    if (HeaderF[0] == 0.0f || (Header[1] & 0x40) != 0)
    {
        if (Block != nullptr)
        {
            std::memcpy(This->Matrix, Block, sizeof(This->Matrix));
            TypedCopyVec3(This->Delta, Block + 9);
            unsigned long Bits = 0;
            std::memcpy(&Bits, &Block[9], sizeof(Bits));
            return Bits;
        }
        const float* Orient = G_AnimEngine->GetInstanceOrientation(Instance);
        std::memcpy(This->Matrix, Orient, sizeof(This->Matrix));
        float* Position = G_AnimEngine->GetInstancePosition(Instance);
        TypedCopyVec3(This->Delta, Position);
        return (unsigned long)((size_t)Position);
    }
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    This->Archetype->Sample(This->Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    This->Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    const double Factor = (double)(This->Field_0x30) * (double)(This->Scale);
    float Pos[3];
    Pos[0] = (float)((double)(Pose[0]) * Factor);
    Pos[1] = (float)((double)(Pose[1]) * Factor);
    Pos[2] = (float)((double)(Pose[2]) * Factor);
    I3DMathEngine* Math = Acquire3DMathEngine();
    float Xformed[3];
    if (Block != nullptr)
    {
        std::memcpy(This->Matrix, Block, sizeof(This->Matrix));
        MathTransformMatrix(Math, Xformed, This->Matrix, Pos);
        This->Delta[0] = Block[9] - Xformed[0];
        This->Delta[1] = Block[10] - Xformed[1];
        This->Delta[2] = Block[11] - Xformed[2];
    }
    else
    {
        float QuatMatrix[9];
        MathQuatToMatrix(Math, QuatMatrix, Pose + 3);
        float Transposed[9];
        Transposed[0] = QuatMatrix[0];
        Transposed[1] = QuatMatrix[3];
        Transposed[2] = QuatMatrix[6];
        Transposed[3] = QuatMatrix[1];
        Transposed[4] = QuatMatrix[4];
        Transposed[5] = QuatMatrix[7];
        Transposed[6] = QuatMatrix[2];
        Transposed[7] = QuatMatrix[5];
        Transposed[8] = QuatMatrix[8];
        const float* Orient = G_AnimEngine->GetInstanceOrientation(Instance);
        float Composed[9];
        MathMulMatrix(Math, Composed, Orient, Transposed);
        std::memcpy(This->Matrix, Composed, sizeof(This->Matrix));
        MathTransformMatrix(Math, Xformed, This->Matrix, Pos);
        const float* Position = G_AnimEngine->GetInstancePosition(Instance);
        This->Delta[0] = Position[0] - Xformed[0];
        This->Delta[1] = Position[1] - Xformed[1];
        This->Delta[2] = Position[2] - Xformed[2];
    }
    unsigned long Bits = 0;
    std::memcpy(&Bits, &This->Delta[1], sizeof(Bits));
    return Bits;
}

static unsigned long DACOM_API TypedGetFlags(FTypedChannel* This)
{
    if (This == nullptr || This->Archetype == nullptr)
        return 0;
    return This->Archetype->Flags;
}

static unsigned long ChannelPayloadBytesFromFlags(unsigned long Flags)
{
    unsigned long Bytes = 0;
    if ((Flags & 1) != 0)
        Bytes = 4;
    if ((Flags & 2) != 0)
        Bytes += 0xC;
    if ((Flags & 4) != 0)
        Bytes += 0x10;
    if ((Flags & 8) != 0)
        Bytes = 4;
    if ((Flags & 0x40) != 0)
        Bytes += 6;
    if ((Flags & 0x80) != 0)
        Bytes += 6;
    if ((Flags & 0x100) != 0)
        Bytes += 8;
    return Bytes;
}

static unsigned long DACOM_API TypedBindMap(FTypedChannel* This, unsigned long* Map, void* Bind)
{
    This->Kind = Map[0];
    This->Field0C = Map[1];
    This->Field10 = Map[2];
    This->Field14 = Map[3];
    This->Bind = Bind;
    if (This->Owner != nullptr)
        return 1;
    unsigned long Flags = 0;
    This->Archetype->GetFlags(&Flags);
    const unsigned long Bytes = ChannelPayloadBytesFromFlags(Flags);
    if (This->Kind == 0 && G_AnimEngine != nullptr)
    {
        unsigned long* Relative = G_AnimEngine->GetInstanceRelative(
            (void*)(This->Field14));
        unsigned long MixKind = 0;
        if (Relative != nullptr)
        {
            const unsigned long Case = Relative[0] - 1;
            if (Case == 0 || Case == 1)
                MixKind = 1;
            else if (Case == 2)
                MixKind = 2;
            else if (Case == 4)
                MixKind = 3;
            else if (Case == 3)
                MixKind = 4;
            else if (Case == 5)
                MixKind = 7;
        }
        if (MixKind * 4 == Bytes)
            return 1;
        return 0;
    }
    if (This->Kind == 2)
        return Flags == 8 ? 1 : 0;
    return Bytes == 0x1C ? 1 : 0;
}

static unsigned long DACOM_API TypedBindMapType1(FScalarTypedChannel* This, unsigned long* Map, void* Bind)
{
    const unsigned long bBound = TypedBindMap(This, Map, Bind);
    if (bBound != 0)
    {
        unsigned long* Relative = (unsigned long*)(
            G_AnimEngine->GetInstanceRelative((void*)(Map[3])));
        if (Relative != nullptr)
            This->JointType = Relative[0];
    }
    return bBound;
}

static unsigned long DACOM_API TypedNop(FTypedChannel* This)
{
    (void)This;
    return 0;
}

static unsigned long DACOM_API TypedReturnZero(FTypedChannel* This)
{
    (void)This;
    return 0;
}

static unsigned long DACOM_API TypedGetType8(FTypedChannel* This)
{
    (void)This;
    return 8;
}

static unsigned long DACOM_API TypedGetType1(FTypedChannel* This)
{
    (void)This;
    return 1;
}

static unsigned long DACOM_API TypedGetType2(FTypedChannel* This)
{
    (void)This;
    return 2;
}

static unsigned long DACOM_API TypedGetType4(FTypedChannel* This)
{
    (void)This;
    return 4;
}

static unsigned long DACOM_API TypedWrapLerpType2(FScalarTypedChannel* This, float* Out, const float* A,
                                                 const float* B, double Alpha)
{

    const double Left = A[0];
    const double Right = B[0];
    double Sum = 0.0;

    if (This->JointType == 2)
    {
        const double Delta = Right - Left;
        const double NegPi = (double)(-3.14159274f);
        const double Pi = (double)(3.14159274f);
        const double TwoPi = (double)(6.28318548f);
        double Target = Right;
        if (Delta < NegPi)
            Target = Right + TwoPi;
        else if (Delta > Pi)
            Target = Right - TwoPi;
        Sum = (Target - Left) * Alpha + Left;
    }
    else
        Sum = (Right - Left) * Alpha + Left;
    Out[0] = (float)Sum;
    Out[0] = This->Scale * Out[0];

    return (unsigned long)((size_t)Out);
}

static unsigned long DACOM_API TypedSlerpType4(FQuatTypedChannel* This, float* Out, const float* Left,
                                              const float* Right, double Alpha)
{
    float RightCopy[4];
    RightCopy[0] = Right[0];
    RightCopy[1] = Right[1];
    RightCopy[2] = Right[2];
    RightCopy[3] = Right[3];
    float LeftCopy[4];
    LeftCopy[0] = Left[0];
    LeftCopy[1] = Left[1];
    LeftCopy[2] = Left[2];
    LeftCopy[3] = Left[3];
    I3DMathEngine* Math = Acquire3DMathEngine();
    float Result[4];
    MathQuatNlerp(Math, Result, LeftCopy, RightCopy, (float)Alpha);
    Out[0] = Result[0];
    Out[1] = Result[1];
    Out[2] = Result[2];
    Out[3] = Result[3];
    unsigned long ScaleBits = 0;
    std::memcpy(&ScaleBits, &This->Scale, sizeof(ScaleBits));
    if (ScaleBits == 0x3F800000)
        return (unsigned long)((size_t)Out);
    Result[0] = 1.0f;
    Result[1] = 0.0f;
    Result[2] = 0.0f;
    Result[3] = 0.0f;
    MathQuatNlerp(Math, LeftCopy, Result, Out, This->Scale);
    Out[0] = LeftCopy[0];
    Out[1] = LeftCopy[1];
    Out[2] = LeftCopy[2];
    Out[3] = LeftCopy[3];
    return (unsigned long)((size_t)Out);
}

static void TypedStoreMatrix3x3(float* Dest, const float* M00)
{
    if (Dest == nullptr || M00 == nullptr)
        return;
    unsigned long Index = 0;
    while (Index < 9)
    {
        Dest[Index] = M00[Index];
        Index += 1;
    }
}

static unsigned long DACOM_API TypedLerpScaledVec3(FVectorTypedChannel* This, float* Out, const float* A,
                                                  const float* B, double Alpha)
{
    const double D0 = (double)(B[0]) - (double)(A[0]);
    const double D1 = (double)(B[1]) - (double)(A[1]);
    const float D2 = (float)((double)(B[2]) - (double)(A[2]));
    const float Blend = (float)Alpha;
    const float T0 = (float)(D0 * (double)Blend);
    const double T1 = D1 * (double)Blend;
    const double T2 = (double)D2 * (double)Blend;
    const float Out0 = (float)((double)T0 + (double)(A[0]));
    const float Out1 = (float)(T1 + (double)(A[1]));
    const float Out2 = (float)(T2 + (double)(A[2]));
    Out[0] = Out0;
    Out[1] = Out1;
    Out[2] = Out2;
    const double Factor = (double)(This->Field_0x30) * (double)(This->Scale);
    Out[0] = (float)(Factor * (double)(Out[0]));
    Out[1] = (float)(Factor * (double)(Out[1]));
    Out[2] = (float)(Factor * (double)(Out[2]));
    return (unsigned long)((size_t)Out);
}

// formerly TypedNopRet10; TODO: derive the retail return contract.
static unsigned long DACOM_API NoOpTypedChannelApply(FTypedChannel* This, const float* Left,
                                            const float* Right, double Alpha)
{
    (void)This;
    (void)Left;
    (void)Right;
    (void)Alpha;
    return 0;
}

// formerly TypedNopSlot0; TODO: derive the retail return contract.
static unsigned long DACOM_API NoOpTypedChannelInterpolate(FTypedChannel* This, float* Out, const float* Left,
                                             const float* Right, double Alpha)
{
    (void)This;
    (void)Out;
    (void)Left;
    (void)Right;
    (void)Alpha;
    return 0;
}

// formerly TypedNopArg1; TODO: derive the retail return contract.
static unsigned long DACOM_API NoOpTypedChannelCopy(FTypedChannel* This, void* A)
{
    (void)This;
    (void)A;
    return 0;
}

// formerly TypedNopArg2; TODO: derive the retail return contract.
static unsigned long DACOM_API NoOpTypedChannelMapped(FTypedChannel* This, void* A, void* B)
{
    (void)This;
    (void)A;
    (void)B;
    return 0;
}

// LAB_06612170 RET 0x8: zero three floats through the first argument, ignore the second.
// Slot 0x38 of every table except type-6's, whose LAB_0661bd30 shows the second argument is a
// float -- so that is how it is spelled here, one signature for the slot.
static unsigned long DACOM_API TypedZeroVec3(FTypedChannel* This, float* Out, float Dt)
{
    (void)This;
    (void)Dt;
    if (Out == nullptr)
        return 0;
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
    return 0;
}

// FUN_0661af70 thiscall RET 4 (Header): header[0] is the start time.
static unsigned long DACOM_API TypedTimePlayable(FTypedChannel* This, float* Time)
{
    unsigned long Bits = 0;
    std::memcpy(&Bits, Time, sizeof(Bits));
    if (Bits == 0xBF800000)
        return 1;
    if (*Time == 0.0f)
        return 1;
    if (Bits == 0xC0000000)
        return 1;
    float Duration = 0.0f;
    This->Archetype->GetDuration(&Duration);                     // 0661af94..9f vtbl+0x10
    if (!(*Time <= Duration))
        return 0;
    if (!(*Time >= 0.0f))
        return 0;
    return 1;
}

// FUN_0661ad90 thiscall RET 4: FUN_0661af70; if AL != 0, virtual slot 1 (Bind) with the same
// header (0661ada5..aa). Returns the check's AL.
static unsigned long DACOM_API TypedTryBindIfTimeValid(FTypedChannel* This, float* Header)
{
    const unsigned long Ok = TypedTimePlayable(This, Header);
    if (Ok != 0)
    {
        This->BindHeader((unsigned long*)Header);
    }
    return Ok;
}

// FUN_0661b070 thiscall RET 0: pose the channel at its current time -- or, once it has
// stopped (PlayFlags bit 1), at the end it ran into -- without advancing it.
void TypedApplyFromArchetype(FTypedChannel* This)
{
    This->PlayFlags |= 1;                                        // 0661b076..81
    if (This->Archetype == nullptr)                              // 0661b07f..84
        return;
    unsigned long Flags = 0;
    This->Archetype->GetFlags(&Flags);                           // 0661b08a..92 vtbl+0x14
    if (Flags == 8)                                              // 0661b095..9a
        return;
    float Time = This->Time;                                     // 0661b09c..9f
    if ((This->PlayFlags & 2) != 0)                              // 0661b0a3..a8 (byte test)
    {
        // FLD Weight; FCOMP qword 0.0; TEST AH,5; JP -- taken for "greater", "equal" and
        // unordered (C0 clear, or C0 and C2 both set): Duration. Only Weight < 0 gives 0.
        if (This->Weight < 0.0f)
            Time = 0.0f;                                         // 0661b0ba
        else
        {
            float Duration = 0.0f;
            This->Archetype->GetDuration(&Duration);             // 0661b0c4..cf vtbl+0x10
            Time = Duration;
        }
    }
    const unsigned char* Left = nullptr;
    const unsigned char* Right = nullptr;
    float Alpha = 0.0f;
    This->Archetype->Sample(Time, &Left, &Right, &Alpha);        // 0661b0da..f4 vtbl+0x18
    // Retail's FUN_0661b070 pushes Alpha as a DOUBLE here -- `SUB ESP,8; FSTP qword [ESP]` --
    This->Apply((const float*)Left,
                                     (const float*)Right, Alpha);
}

// FUN_0661b120 thiscall RET 0x18 (T0, T1, Mode, Buf, Cap, Count): gather the keys an event
static unsigned long TypedCollect(FTypedChannel* This, float T0, float T1, unsigned long Mode,
                                  float* Buf, unsigned long Cap, unsigned long* Count)
{
    float Duration = 0.0f;
    This->Archetype->GetDuration(&Duration);                     // 0661b12d..36 vtbl+0x10
    unsigned long Found = 0;                                     // frame +0x10

    // 0661b139..5c / 0661b228..37. Weight: FCOMP qword 0.0; TEST AH,5; JP -- taken for >= 0
    unsigned char bWrap = 0;
    if (!(This->Weight < 0.0f))
    {
        if (!(T1 < Duration || T1 != T1))
            bWrap = 1;
    }
    else if (T1 <= 0.0f)
        bWrap = 1;

    if (bWrap == 0)
    {
        Found = 0;                                               // 0661b16e
        if ((Mode & 1) != 0)                                     // 0661b172..97
        {
            This->Archetype->CollectAt(T0, Buf, Cap, &Found);
            *Count += Found;
        }
        // FLD T0; FCOMP T1; TEST AH,0x44; JNP -- "equal" skips the range.
        if (T0 != T1)                                            // 0661b199..a6
        {
            Found = 0;
            This->Archetype->CollectRange(T0, T1, Buf + *Count * 2, Cap - *Count, &Found);
            *Count += Found;                                     // 0661b1a8..e1
            if ((Mode & 2) != 0)                                 // 0661b1df..0661b213
            {
                Found = 0;
                This->Archetype->CollectAt(T1, Buf + *Count * 2, Cap - *Count, &Found);
                *Count += Found;
            }
        }
        This->Time = T1;                                         // 0661b215..1a
        return 0;
    }

    // 0661b23d..71: the span up to the end the channel is running towards.
    float Span = 0.0f;
    if (This->Weight < 0.0f)
        Span = -T0;                                              // FLD T0; FCHS
    else
        Span = (float)((double)Duration - T0);
    Found = 0;                                                   // 0661b269
    if ((Mode & 1) != 0)                                         // 0661b275..96
    {
        This->Archetype->CollectAt(T0, Buf, Cap, &Found);
        *Count += Found;
    }
    // FLD Span; FCOMP 0.0f; TEST AH,0x44; JNP -- a zero span skips the range. Found is not
    // cleared before either of the two calls below (0661b2a9..0661b317).
    if (Span != 0.0f)
    {
        const float EndTime = (float)((double)Span + T0);
        This->Archetype->CollectRange(T0, EndTime, Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        if ((Mode & 2) != 0)
        {
            This->Archetype->CollectAt(EndTime, Buf + *Count * 2, Cap - *Count, &Found);
            *Count += Found;
        }
    }

    const unsigned long Loop = This->LoopFlags;                  // 0661b319
    const bool bForward = !(This->Weight < 0.0f);                // 0661b31c..1f, TEST AH,5 / JP
    if ((Loop & 2) != 0)                                         // 0661b325..0661b389
    {
        Found = 0;
        if (bForward)
            TypedCollect(This, 0.0f, (float)((double)T1 - Duration), 3,
                         Buf + *Count * 2, Cap - *Count, &Found);
        else
            TypedCollect(This, Duration, (float)((double)Duration + T1), 3,
                         Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        return 0;
    }
    if ((Loop & 4) != 0)                                         // 0661b38c..0661b3f2
    {
        const float Turn = bForward ? Duration : 0.0f;
        This->Weight = -This->Weight;                            // FLD; FCHS; FSTP
        Found = 0;
        TypedCollect(This, Turn, (float)((double)Turn - T1), 2,
                     Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        return 0;
    }
    This->Time = bForward ? Duration : 0.0f;                     // 0661b3f5..0a
    This->PlayFlags |= 2;                                        // 0661b40d..0f
    return 1;
}

// FUN_0661b420 thiscall RET 4 (Dt): advance the channel by Dt * Weight and pose it. Answers 1
// when a non-looping channel has run off its end this tick and nothing has posed it since
// (PlayFlags bit 0 clear); an owner-driven channel answers whatever its owner does.
unsigned long TypedTick(FTypedChannel* This, float Dt)
{
    unsigned long Result = 0;                                    // frame +0x10
    This->PlayFlags |= 4;                                        // 0661b42c..38
    if ((This->PlayFlags & 3) != 0)                              // 0661b43b..3d
        return 0;

    const float OldTime = This->Time;                            // 0661b44d..51
    // FLD Dt; FMUL Weight; FADD Time; FST local; FSTP Time.
    const float NewTime = (float)((double)Dt * This->Weight + This->Time);
    This->Time = NewTime;

    if (This->Owner != nullptr)                                  // 0661b45b..64
    {
        float* Sampled = This->GetSampled();
        IChannelOwner* Owner = This->Owner;
        return (unsigned long)(Owner->Apply(
            Sampled, (unsigned long)((size_t)This),
            TypedBindDesc(This), NewTime));                       // 0661b466..7e
    }

    unsigned long Flags = 0;
    This->Archetype->GetFlags(&Flags);                           // 0661b490..9b vtbl+0x14
    if (Flags == 8)                                              // 0661b49e..a3
    {
        float Keys[0x40][2];                                     // frame +0x2A4
        unsigned long KeyCount = 0;
        Result = TypedCollect(This, OldTime, This->Time, 3, &Keys[0][0], 0x40, &KeyCount);
        if (KeyCount == 0)                                       // 0661b4d6..d8
            return Result;
        void* Sink = (void*)((size_t)(This->Field0C));
        if (Sink == nullptr)                                     // 0661b4de..e3
            return Result;
        FChannelKeyBatch Batch;                                  // frame +0xA0
        Batch.Count = KeyCount;                                  // 0661b4eb
        // REP MOVSD of (count & 0x1FFFFFFF) * 2 dwords (0661b4f4..0661b50a).
        std::memcpy(Batch.Keys, Keys, (KeyCount & 0x1FFFFFFFu) * 2 * sizeof(float));
        IChannelSink* KeySink = (IChannelSink*)Sink;
        KeySink->OnKeys((unsigned long)((size_t)This),
                        (void*)((size_t)(This->Field10)),
                        &Batch);                                  // 0661b50c..1c
        return Result;
    }

    float Duration = 0.0f;
    This->Archetype->GetDuration(&Duration);                     // 0661b52e..39 vtbl+0x10
    // FLD Weight; FCOMP qword 0.0; TEST AH,5; JP -- >= 0 and unordered take Duration - Time.
    const double Remaining = This->Weight < 0.0f
                                 ? (double)(This->Time)
                                 : (double)Duration - This->Time;
    // FCOM qword 0.0; TEST AH,0x41; JP -- "greater" and unordered go straight to sampling.
    if (Remaining <= 0.0)
    {
        // FLD Duration; FCOMP 0.0f; TEST AH,0x44; JNP -- a zero-length clip keeps 0 wraps.
        unsigned long Wraps = 0;                                 // EBX
        if (Duration != 0.0f)
        {
            // FDIVR -> Remaining / Duration; _ftol; CDQ / XOR / SUB: |low dword|.
            const long Low = (long)((long long)(Remaining / Duration));
            Wraps = (unsigned long)((Low ^ (Low >> 31)) - (Low >> 31));
        }
        // FABS; FILD qword {Wraps, 0}; FMUL Duration; FSUBP; FSTP dword.
        const float Frac = (float)(std::fabs(Remaining) -
                                              (double)Wraps * Duration);
        const unsigned long Loop = This->LoopFlags;              // 0661b59d
        unsigned long Header[7];                                 // frame +0x1C
        if ((Loop & 2) != 0)                                     // 0661b5a0..ad
        {
            // Frame +0x50: a 3x3 orientation (0..8), the applied position (9..11) and the
            // matrix QuaternionToMatrix writes (12..20), which is copied over 0..8.
            float Frame[21];
            if (This->Kind == 1)                                 // 0661b5b3..bd
            {
                // bLast = Weight >= 0 (or unordered): TEST AH,5; JP -> ECX 0 -> SETZ 1.
                const unsigned char bLast = This->Weight < 0.0f ? 0 : 1;
                const unsigned char* FrameData = nullptr;
                This->Archetype->GetFrame(bLast, &FrameData);    // 0661b5d9..ea vtbl+0x20
                This->Apply(
                    (const float*)FrameData,
                    (const float*)FrameData,
                    (double)Dt);                    // 0661b5ed..0661b605
                // Retail reads +0x78 / +0x84 straight off the object, whatever its class. A
                FPosQuatTypedChannel* Pose = (FPosQuatTypedChannel*)This;
                Frame[9] = Pose->AppliedPos[0];                  // 0661b608..1c
                Frame[10] = Pose->AppliedPos[1];
                Frame[11] = Pose->AppliedPos[2];
                I3DMathEngine* Math = Acquire3DMathEngine();     // DAT_0662acf4
                Math->QuaternionToMatrix(*(FMatrix*)(&Frame[12]),
                                         *(const FQuaternion*)(Pose->AppliedQuat));
                std::memcpy(Frame, &Frame[12], 9 * sizeof(float)); // 0661b648..5d
                void* Sink = (void*)((size_t)(This->Field0C));
                if (Sink != nullptr)                             // 0661b65b..6e
                {
                    IChannelSink* LoopSink = (IChannelSink*)Sink;
                    LoopSink->OnLoop((unsigned long)((size_t)This),
                                     Frame,
                                     (void*)((size_t)(This->Field10)));
                }
            }
            // 0661b673..0661b6d6: rebind at the start the channel wraps back to.
            float StartTime = 0.0f;
            if (This->Weight < 0.0f)
                StartTime = Duration;
            std::memcpy(&Header[0], &StartTime, sizeof(float));
            Header[1] = Loop;
            std::memcpy(&Header[2], &This->Weight, sizeof(float));
            Header[3] = 0;
            std::memcpy(&Header[4], &This->Scale, sizeof(float));
            std::memcpy(&Header[5], &This->Field_0x30, sizeof(float));
            Header[6] = This->Kind == 1
                            ? (unsigned long)((size_t)(&Frame[0]))
                            : 0;
            This->BindHeader(Header);
            // FLD Duration (frame +0x10); FCOMP 0.0f; TEST AH,0x44; JNP -- skipped only when the
            // clip has zero length. Then Weight >= 0 (or unordered) -> Frac, else Duration - Frac.
            if (Duration != 0.0f)                                // 0661b6d9..0661b715
            {
                if (This->Weight < 0.0f)
                    This->Time = (float)((double)Duration - Frac);
                else
                    This->Time = Frac;
            }
        }
        else if ((Loop & 4) != 0)                                // 0661b71a..0661b77b
        {
            // Ping-pong: an odd number of wraps keeps the direction, an even one reverses it.
            float NewWeight = This->Weight;
            if ((Wraps & 1) == 0)                                // TEST BL,1; JNZ; FCHS
                NewWeight = -NewWeight;
            float StartTime = Frac;                              // FCOMP 0.0f; TEST AH,5; JP
            if (NewWeight < 0.0f)
                StartTime = (float)((double)Duration - Frac);
            std::memcpy(&Header[0], &StartTime, sizeof(float));
            Header[1] = Loop;
            std::memcpy(&Header[2], &NewWeight, sizeof(float));
            Header[3] = 0;
            std::memcpy(&Header[4], &This->Scale, sizeof(float));
            std::memcpy(&Header[5], &This->Field_0x30, sizeof(float));
            Header[6] = 0;
            This->BindHeader(Header);
        }
        else                                                     // 0661b77d..b4
        {
            This->Time = This->Weight < 0.0f ? 0.0f : Duration;
            This->PlayFlags |= 2;
            if ((This->PlayFlags & 1) == 0)                      // TEST BL,AL with BL = 1
                Result = 1;
        }
    }

    const unsigned char* Left = nullptr;
    const unsigned char* Right = nullptr;
    float Alpha = 0.0f;
    This->Archetype->Sample(This->Time, &Left, &Right, &Alpha);  // 0661b7b8..d1 vtbl+0x18
    This->Apply((const float*)Left,
                                     (const float*)Right,
                                     (double)Alpha); // 0661b7d4..ed
    return Result;
}

// LAB_0661caf0, the type-6 table's slot 8 (+0x20, .rdata:06629110), thiscall RET 0x4 (Target),
// formerly TypedEvalType6Caf0
static unsigned long DACOM_API SetChannelOverlayPosition(FPosQuatTypedChannel* This, const float* Target)
{
    if (This->Kind != 1)
        return 0;
    const float* KeyA = nullptr;
    const float* KeyB = nullptr;
    float Alpha = 0.0f;
    This->Archetype->Sample(This->Time, (const unsigned char**)(&KeyA),
                            (const unsigned char**)(&KeyB), &Alpha);
    const double D0 = (double)(KeyB[0]) - (double)(KeyA[0]);
    const double D1 = (double)(KeyB[1]) - (double)(KeyA[1]);
    const float D2 = KeyB[2] - KeyA[2];
    const float V0 = (float)(D0 * (double)Alpha);
    const float P0 = V0 + KeyA[0];
    const float P1 = (float)(D1 * (double)Alpha + (double)(KeyA[1]));
    const double P2 = (double)D2 * (double)Alpha + (double)(KeyA[2]);
    const float P0Scaled = P0 * This->Scale;
    const float P1Scaled = P1 * This->Scale;
    const double P2Scaled = P2 * (double)(This->Scale);
    float Pos[3];
    Pos[0] = P0Scaled * This->Field_0x30;
    Pos[1] = P1Scaled * This->Field_0x30;
    Pos[2] = (float)(P2Scaled * (double)(This->Field_0x30));
    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, Pos);
    This->Delta[0] = Target[0] - Xformed[0];
    This->Delta[1] = Target[1] - Xformed[1];
    This->Delta[2] = Target[2] - Xformed[2];
    This->AppliedPos[0] = Target[0];
    This->AppliedPos[1] = Target[1];
    This->AppliedPos[2] = Target[2];
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Target[2], sizeof(Bits));
    return Bits;
}

// LAB_0661bc80 RET 0x14: lerp pos + 3DMath +0x58 slerp quat. Not IK.
static unsigned long DACOM_API TypedLerpPosQuat(FPosQuatTypedChannel* This, float* Out, const float* Left,
                                               const float* Right, double Alpha)
{
    (void)This;
    Out[0] = (float)(((double)(Right[0]) - Left[0]) * Alpha + Left[0]);
    Out[1] = (float)(((double)(Right[1]) - Left[1]) * Alpha + Left[1]);
    Out[2] = (float)(((double)(Right[2]) - Left[2]) * Alpha + Left[2]);
    const float Blend = (float)Alpha;
    I3DMathEngine* Math = Acquire3DMathEngine();
    float Quat[4];
    MathQuatNlerp(Math, Quat, Left + 3, Right + 3, Blend);
    Out[3] = Quat[0];
    Out[4] = Quat[1];
    Out[5] = Quat[2];
    Out[6] = Quat[3];
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Quat[1], sizeof(Bits));
    return Bits;
}

// LAB_0661ba60, the type-6 table's slot 0 (.rdata:PTR_LAB_066290f0[0]). Recovered
static unsigned long DACOM_API TypedLerpPosQuatScaled(FPosQuatTypedChannel* This, float* Out,
                                                      const float* Left, const float* Right,
                                                      double Alpha)
{
    This->InterpolatePose(Out, Left, Right, Alpha);
    unsigned long ScaleBits = 0;
    std::memcpy(&ScaleBits, &This->Scale, sizeof(ScaleBits));
    if (ScaleBits == 0x3F800000)
        return ScaleBits;
    I3DMathEngine* Math = Acquire3DMathEngine();
    float const Identity[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float Nlerped[4];
    MathQuatNlerp(Math, Nlerped, Identity, Out + 3, This->Scale);
    Out[3] = Nlerped[0];
    Out[4] = Nlerped[1];
    Out[5] = Nlerped[2];
    Out[6] = Nlerped[3];
    unsigned long LastBits = 0;
    std::memcpy(&LastBits, &Nlerped[3], sizeof(LastBits));
    return LastBits;
}

// LAB_0661b880, slot 2 of the type 1 / 2 / 4 tables (.rdata:066291ac, 06629170, 06629134; one
// formerly TypedApplySlot2
static unsigned long DACOM_API ApplyInterpolatedChannelSample(FTypedChannel* This, float* Sampled,
                                               const float* Left, const float* Right, double Alpha)
{
    return This->InterpolatePose(Sampled, Left, Right, Alpha);
}

// LAB_0661bb10, the type-6 table's slot 2 (.rdata:066290f8), thiscall RET 0x10 (Left, Right,
// formerly TypedApplySlot2Type6
static unsigned long DACOM_API ApplyPositionQuaternionSample(FPosQuatTypedChannel* This, const float* Left,
                                                    const float* Right, double Alpha)
{
    float Local[7];
    This->Interpolate(Local, Left, Right, Alpha);
    const unsigned long Kind = This->Kind;
    if (Kind == 1)
    {
        const double Factor = (double)(This->Field_0x30) * (double)(This->Scale);
        float Pos[3];
        Pos[0] = (float)((double)(Local[0]) * Factor);
        Pos[1] = (float)((double)(Local[1]) * Factor);
        Pos[2] = (float)((double)(Local[2]) * Factor);
        I3DMathEngine* Math = Acquire3DMathEngine();
        float Xformed[3];
        MathTransformMatrix(Math, Xformed, This->Matrix, Pos);
        This->AppliedPos[0] = Xformed[0] + This->Delta[0];
        This->AppliedPos[1] = Xformed[1] + This->Delta[1];
        This->AppliedPos[2] = Xformed[2] + This->Delta[2];
        float MatrixQuat[4];
        MathMatrixToQuat(Math, MatrixQuat, This->Matrix);
        float Composed[4];
        Math->MulQuat(*(FQuaternion*)Composed,
                              *(const FQuaternion*)MatrixQuat,
                              *(const FQuaternion*)(Local + 3));   // +0x18
        This->AppliedQuat[0] = Composed[0];
        This->AppliedQuat[1] = Composed[1];
        This->AppliedQuat[2] = Composed[2];
        This->AppliedQuat[3] = Composed[3];
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Composed[3], sizeof(Bits));
        return Bits;
    }
    if (Kind == 0)
    {
        TypedCopyVec3(This->AppliedPos, Local);
        This->AppliedQuat[0] = Local[3];
        This->AppliedQuat[1] = Local[4];
        This->AppliedQuat[2] = Local[5];
        This->AppliedQuat[3] = Local[6];
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Local[6], sizeof(Bits));
        return Bits;
    }
    return Kind;
}

// LAB_0661afe0 thiscall RET 0x4 (Time), 0661afe0..66. Each test is FCOMP + TEST AH,0x44:
static unsigned long DACOM_API TypedSetTime(FTypedChannel* This, float Time)
{
    if (Time == -1.0f)
    {
        float Duration = Time;
        This->Archetype->GetDuration(&Duration);
        This->Time = Duration;
        return 1;
    }
    if (Time == 0.0f)
    {
        This->Time = 0.0f;
        return 1;
    }
    if (Time == -2.0f)
        return 1;
    if (Time < 0.0f)
        return 0;
    This->Time = Time;
    return 1;
}

// LAB_0661cf90: MOV EAX,6; RET.
static unsigned long DACOM_API TypedGetType6(FTypedChannel* This)
{
    (void)This;
    return 6;
}

// LAB_06612190 RET 4 (06612190..99): MOV EAX,[ECX+0x3C]; *Out = EAX. EAX -- the copied dword
// -- is the answer. No null checks.
static unsigned long DACOM_API TypedCopySampledDword(FScalarTypedChannel* This, float* Out)
{
    Out[0] = This->Sampled[0];
    unsigned long Bits = 0;
    std::memcpy(&Bits, &This->Sampled[0], sizeof(Bits));
    return Bits;
}

// LAB_066121b0 RET 4 (066121b0..cd): copy 16 bytes from this+0x3C; EAX = Out is the answer.
// No null checks. Type-4 slot 6.
static unsigned long DACOM_API TypedCopySampled16(FQuatTypedChannel* This, float* Out)
{
    Out[0] = This->Sampled[0];
    Out[1] = This->Sampled[1];
    Out[2] = This->Sampled[2];
    Out[3] = This->Sampled[3];
    return (unsigned long)((size_t)Out);
}

// LAB_066121d0 RET 4 (066121d0..e7): copy 12 bytes from this+0x3C; EAX = Out. No null checks.
// Type-2 slot 6.
static unsigned long DACOM_API TypedCopySampled12(FVectorTypedChannel* This, float* Out)
{
    TypedCopyVec3(Out, This->Sampled);
    return (unsigned long)((size_t)Out);
}

// LAB_066121f0 RET 0x4 (066121f0..2e): copy 12 bytes from +0x78 and 16 from +0x84 into
// Out / Out+0xC; EAX = Out+0xC is the answer. No null checks.
static unsigned long DACOM_API TypedCopyApplied(FPosQuatTypedChannel* This, float* Out)
{
    TypedCopyVec3(Out, This->AppliedPos);
    Out[3] = This->AppliedQuat[0];
    Out[4] = This->AppliedQuat[1];
    Out[5] = This->AppliedQuat[2];
    Out[6] = This->AppliedQuat[3];
    return (unsigned long)((size_t)(Out + 3));
}

// LAB_0661c6d0 thiscall RET 0x4 (Add), 0661c6d0..f9: Delta (+0x60) [i] = float(Delta[i] +
static unsigned long DACOM_API TypedAddDelta(FPosQuatTypedChannel* This, const float* Add)
{
    This->Delta[0] += Add[0];
    This->Delta[1] += Add[1];
    This->Delta[2] += Add[2];
    if ((This->PlayFlags & G_ChannelPlayFlag) != 0)
        TypedApplyFromArchetype(This);
    return (unsigned long)((size_t)Add);
}

// LAB_06612240: LEA EAX,[ECX+0x78]; RET. No null check.
static float* DACOM_API TypedAppliedPtr(FPosQuatTypedChannel* This)
{
    return This->AppliedPos;
}

// LAB_0661c5c0, the type-6 table's slot 0x30, thiscall RET 0x8 (Out, OutSampled),
static unsigned long DACOM_API TypedSampleXformType6(FPosQuatTypedChannel* This, float* Out,
                                                     float* OutSampled)
{
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    if ((This->PlayFlags & G_ChannelBlendFlag) != 0)
    {
        const unsigned char bLast = (double)(This->Weight) < 0.0 ? 0 : 1;
        This->Archetype->GetFrame(bLast, &KeyA);
        Alpha = 0.0f;
        KeyB = KeyA;
    }
    else
        This->Archetype->Sample(This->Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    This->Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    OutSampled[0] = Pose[0];
    OutSampled[1] = Pose[1];
    OutSampled[2] = Pose[2];
    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, OutSampled);
    Out[0] = Xformed[0] + This->Delta[0];
    Out[1] = Xformed[1] + This->Delta[1];
    Out[2] = Xformed[2] + This->Delta[2];
    return (unsigned long)((size_t)Out);
}

// One pass of LAB_0661c320 (retail inlines it twice, 0661c34d..0661c423 and
static void TypedType6SampleXform(FPosQuatTypedChannel* This, float Time, const float** KeyA,
                                  const float** KeyB, float* Alpha, float* Xformed)
{
    This->Archetype->Sample(Time, (const unsigned char**)KeyA,
                            (const unsigned char**)KeyB, Alpha);
    const float* A = *KeyA;
    const float* B = *KeyB;
    float V[3];
    V[0] = (B[0] - A[0]) * *Alpha;
    V[1] = (B[1] - A[1]) * *Alpha;
    V[2] = (B[2] - A[2]) * *Alpha;
    V[0] = V[0] + A[0];
    V[1] = V[1] + A[1];
    V[2] = V[2] + A[2];
    const double Factor = (double)(This->Scale) * (double)(This->Field_0x30);
    V[0] = (float)((double)(V[0]) * Factor);
    V[1] = (float)((double)(V[1]) * Factor);
    V[2] = (float)((double)(V[2]) * Factor);
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, V);
}

// LAB_0661c320, the type-6 table's slot 0x34, thiscall RET 0x4 (Dt), 0661c320..0661c5b2.
static unsigned long DACOM_API TypedStepDeltaType6(FPosQuatTypedChannel* This, float Dt)
{
    const float Shifted = (double)(This->Weight) < 0.0
                              ? (float)((double)(This->Time) - (double)Dt)
                              : (float)((double)(This->Time) + (double)Dt);
    const float* KeyA = nullptr;
    const float* KeyB = nullptr;
    float Alpha = 0.0f;
    float Xformed[3];
    TypedType6SampleXform(This, This->Time, &KeyA, &KeyB, &Alpha, Xformed);
    float Now[3];
    Now[0] = Xformed[0] + This->Delta[0];
    Now[1] = Xformed[1] + This->Delta[1];
    Now[2] = Xformed[2] + This->Delta[2];
    TypedType6SampleXform(This, Shifted, &KeyA, &KeyB, &Alpha, Xformed);
    const double S0 = (double)(Xformed[0]) + (double)(This->Delta[0]);
    const double S1 = (double)(Xformed[1]) + (double)(This->Delta[1]);
    const float S2 = (float)((double)(Xformed[2]) + (double)(This->Delta[2]));
    const float Dx = (float)(S0 - (double)(Now[0]));
    const double Dy = S1 - (double)(Now[1]);
    const double Dz = (double)S2 - (double)(Now[2]);
    const double Inv = 1.0 / (double)Dt;
    This->StepDelta[0] = (float)((double)Dx * Inv);
    This->StepDelta[1] = (float)(Dy * Inv);
    This->StepDelta[2] = (float)(Dz * Inv);
    unsigned long Bits = 0;
    std::memcpy(&Bits, &This->StepDelta[1], sizeof(Bits));
    return Bits;
}

// Round each position scaling step to float.
static void TypedType6ScaledFrame(FPosQuatTypedChannel* This, unsigned char bLast, float* Out)
{
    const float* Frame = nullptr;
    This->Archetype->GetFrame(bLast, (const unsigned char**)(&Frame));
    float Scaled[3];
    Scaled[0] = This->Scale * Frame[0];
    Scaled[1] = This->Scale * Frame[1];
    Scaled[2] = This->Scale * Frame[2];
    Out[0] = Scaled[0] * This->Field_0x30;
    Out[1] = Scaled[1] * This->Field_0x30;
    Out[2] = Scaled[2] * This->Field_0x30;
}

// LAB_0661bd30, the type-6 table's slot 0x38, thiscall RET 0x8 (Out, Dt), 0661bd30..0661c26b.
static unsigned long DACOM_API TypedStepPosType6(FPosQuatTypedChannel* This, float* Out, float Dt)
{
    const float WeightedDt = Dt * This->Weight;
    float Duration = 0.0f;
    This->Archetype->GetDuration(&Duration);
    float Step[3];
    Step[0] = 0.0f;
    Step[1] = 0.0f;
    Step[2] = 0.0f;
    const unsigned char bReverse = (double)(This->Weight) < 0.0 ? 1 : 0;
    const double SampleTimeWide = bReverse != 0
                                      ? (double)(This->Time) - (double)WeightedDt
                                      : (double)(This->Time) + (double)WeightedDt;
    const float SampleTime = (float)SampleTimeWide;
    unsigned char bCrossed = 0;
    if (bReverse != 0)
        bCrossed = SampleTimeWide < 0.0 ? 1 : 0;
    else
        bCrossed = SampleTimeWide > (double)Duration ? 1 : 0;

    const float* KeyA = nullptr;
    const float* KeyB = nullptr;
    float Alpha = 0.0f;
    This->Archetype->Sample(This->Time, (const unsigned char**)(&KeyA),
                            (const unsigned char**)(&KeyB), &Alpha);
    float Lerp[3];
    Lerp[0] = KeyB[0] - KeyA[0];
    Lerp[1] = KeyB[1] - KeyA[1];
    Lerp[2] = KeyB[2] - KeyA[2];
    Lerp[0] = Lerp[0] * Alpha;
    Lerp[1] = Lerp[1] * Alpha;
    Lerp[2] = Lerp[2] * Alpha;
    Lerp[0] = KeyA[0] + Lerp[0];
    Lerp[1] = KeyA[1] + Lerp[1];
    Lerp[2] = KeyA[2] + Lerp[2];
    const double Factor = (double)(This->Scale) * (double)(This->Field_0x30);
    float Now[3];
    Now[0] = (float)((double)(Lerp[0]) * Factor);
    Now[1] = (float)((double)(Lerp[1]) * Factor);
    Now[2] = (float)((double)(Lerp[2]) * Factor);

    if (bCrossed != 0 && (This->LoopFlags & 2) != 0)
    {
        float EndA[3];
        TypedType6ScaledFrame(This, bReverse != 0 ? 0 : 1, EndA);
        Step[0] = EndA[0] - Now[0];
        Step[1] = EndA[1] - Now[1];
        Step[2] = EndA[2] - Now[2];
        float EndB[3];
        TypedType6ScaledFrame(This, bReverse, EndB);
        const float Leftover = bReverse != 0 ? Duration - SampleTime : SampleTime - Duration;
        This->Archetype->Sample(Leftover, (const unsigned char**)(&KeyA),
                                (const unsigned char**)(&KeyB), &Alpha);
        Lerp[0] = KeyB[0] - KeyA[0];
        Lerp[1] = KeyB[1] - KeyA[1];
        Lerp[2] = KeyB[2] - KeyA[2];
        Lerp[0] = Lerp[0] * Alpha;
        Lerp[1] = Lerp[1] * Alpha;
        Lerp[2] = Lerp[2] * Alpha;
        Lerp[0] = Lerp[0] + KeyA[0];
        Lerp[1] = Lerp[1] + KeyA[1];
        Lerp[2] = Lerp[2] + KeyA[2];
        float Wrapped[3];
        Wrapped[0] = (float)((double)(Lerp[0]) * Factor - (double)(EndB[0]));
        Wrapped[1] = (float)((double)(Lerp[1]) * Factor - (double)(EndB[1]));
        Wrapped[2] = (float)((double)(Lerp[2]) * Factor - (double)(EndB[2]));
        Step[0] = Wrapped[0] + Step[0];
        Step[1] = Step[1] + Wrapped[1];
        Step[2] = Step[2] + Wrapped[2];
    }
    else if (This->Archetype->Sample(SampleTime, (const unsigned char**)(&KeyA),
                                     (const unsigned char**)(&KeyB), &Alpha) == 0)
    {
        float Diff[3];
        Diff[0] = KeyB[0] - KeyA[0];
        Diff[1] = KeyB[1] - KeyA[1];
        Diff[2] = KeyB[2] - KeyA[2];
        const float Shifted0 = (float)((double)(Diff[0]) * (double)Alpha +
                                                  (double)(KeyA[0]));
        const float Shifted1 = (float)((double)(Diff[1]) * (double)Alpha +
                                                  (double)(KeyA[1]));
        const float Diff2Alpha = Diff[2] * Alpha;
        const float Shifted2 = Diff2Alpha + KeyA[2];
        const float Scaled1 = (float)((double)Shifted1 * Factor);
        const float Scaled2 = (float)((double)Shifted2 * Factor);
        Step[0] = (float)((double)Shifted0 * Factor - (double)(Now[0]));
        Step[1] = Scaled1 - Now[1];
        Step[2] = Scaled2 - Now[2];
    }
    // else: a failed Sample leaves Step zero (0661c20e).

    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, Step);
    Out[0] = Xformed[0];
    Out[1] = Xformed[1];
    Out[2] = Xformed[2];
    return (unsigned long)((size_t)Out);
}

// LAB_0661cc40, the type-6 table's slot 9 (+0x24, .rdata:06629114), thiscall RET 0x4 (Orient:
// formerly TypedEvalType6Cc40
static unsigned long DACOM_API SetChannelOverlayOrientation(FPosQuatTypedChannel* This, const float* Orient)
{
    if (This->Kind != 1)
        return 0;
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    This->Archetype->Sample(This->Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    This->Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    float Pos[3];
    Pos[0] = Pose[0];
    Pos[1] = Pose[1];
    Pos[2] = Pose[2];
    float Quat[4];
    Quat[0] = Pose[3];
    Quat[1] = Pose[4];
    Quat[2] = Pose[5];
    Quat[3] = Pose[6];
    FEngineInstance* Instance = (FEngineInstance*)((size_t)(This->Field14));
    const float* InstanceOrient = G_AnimEngine->GetInstanceOrientation(Instance);
    float Transposed[9];                                         // FUN_0661caa0
    Transposed[0] = InstanceOrient[0];
    Transposed[1] = InstanceOrient[3];
    Transposed[2] = InstanceOrient[6];
    Transposed[3] = InstanceOrient[1];
    Transposed[4] = InstanceOrient[4];
    Transposed[5] = InstanceOrient[7];
    Transposed[6] = InstanceOrient[2];
    Transposed[7] = InstanceOrient[5];
    Transposed[8] = InstanceOrient[8];
    float R1[9];
    MathMulMatrix(Acquire3DMathEngine(), R1, Orient, Transposed);
    float R2[9];
    MathMulMatrix(Acquire3DMathEngine(), R2, R1, This->Matrix);
    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, Pos);
    float World[3];
    World[0] = Xformed[0] + This->Delta[0];
    World[1] = Xformed[1] + This->Delta[1];
    World[2] = Xformed[2] + This->Delta[2];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, R2, Pos);
    This->Delta[0] = World[0] - Xformed[0];
    This->Delta[1] = World[1] - Xformed[1];
    This->Delta[2] = World[2] - Xformed[2];
    std::memcpy(This->Matrix, R2, sizeof(This->Matrix));
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, Pos);
    This->AppliedPos[0] = Xformed[0] + This->Delta[0];
    This->AppliedPos[1] = Xformed[1] + This->Delta[1];
    This->AppliedPos[2] = Xformed[2] + This->Delta[2];
    float QuatMatrix[9];
    MathQuatToMatrix(Acquire3DMathEngine(), QuatMatrix, Quat);
    float R3[9];
    MathMulMatrix(Acquire3DMathEngine(), R3, R2, QuatMatrix);
    MathMatrixToQuat(Acquire3DMathEngine(), This->AppliedQuat, R3);
    unsigned long Bits = 0;
    std::memcpy(&Bits, &This->AppliedQuat[3], sizeof(Bits));
    return Bits;
}

// The typed-channel vtable, as four retail tables actually have it (cross-DLL ABI pass 34).
// The 15 slots below follow retail's __thiscall table order (PTR_LAB_066294ac,
// PTR_LAB_0662912c/168/a4 and PTR_LAB_066290f0). Member dispatch puts this in ECX.
unsigned long FTypedChannel::Interpolate(float* Out, const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelInterpolate(this, Out, Left, Right, Alpha); }
unsigned long FTypedChannel::BindHeader(unsigned long* Header) { return TypedBind(this, Header); }
unsigned long FTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelApply(this, Left, Right, Alpha); }
unsigned long FTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelInterpolate(this, Out, Left, Right, Alpha); }
unsigned long FTypedChannel::SetTime(float Time) { return TypedSetTime(this, Time); }
unsigned long FTypedChannel::GetType() { return TypedGetType8(this); }
unsigned long FTypedChannel::CopySampled(float* Out) { return NoOpTypedChannelCopy(this, Out); }
unsigned long FTypedChannel::BindMap(unsigned long* Map, void* Bind) { return TypedBindMap(this, Map, Bind); }
unsigned long FTypedChannel::SetOverlayPos(const float* Pos) { (void)Pos; return 0; }
unsigned long FTypedChannel::SetOverlayExtra(const float* Extra) { (void)Extra; return 0; }
unsigned long FTypedChannel::AddOverlayPos(const float* Delta) { (void)Delta; return 0; }
float* FTypedChannel::GetSampled() { return nullptr; }
unsigned long FTypedChannel::TouchMapped(void* ArgA, void* ArgB) { return NoOpTypedChannelMapped(this, ArgA, ArgB); }
unsigned long FTypedChannel::UpdateDeltaRate(float Dt) { (void)Dt; return 0; } // formerly Slot13
unsigned long FTypedChannel::Step(float* Out, float Dt) { return TypedZeroVec3(this, Out, Dt); }

// Type 1 (066291a4): slots 0/1/4/8..10/12..14 inherited (the shared bodies), 2 LAB_0661b880,
// 3 LAB_0661b8a0, 5 LAB_0661b870, 6 LAB_06612190, 7 FUN_0661b830, 11 LAB_066121a0.
unsigned long FScalarTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
unsigned long FScalarTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return TypedWrapLerpType2(this, Out, Left, Right, Alpha); }
unsigned long FScalarTypedChannel::GetType() { return TypedGetType1(this); }
unsigned long FScalarTypedChannel::CopySampled(float* Out) { return TypedCopySampledDword(this, Out); }
unsigned long FScalarTypedChannel::BindMap(unsigned long* Map, void* Bind) { return TypedBindMapType1(this, Map, Bind); }
float* FScalarTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0: LEA EAX,[ECX+0x3C]

// Type 2 (06629168): 2 LAB_0661b880, 3 LAB_0661cfa0, 5 LAB_0661d040, 6 LAB_066121d0,
// 7 FUN_0661adc0 (the base's), 11 LAB_066121a0; the rest as the base.
unsigned long FVectorTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
unsigned long FVectorTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return TypedLerpScaledVec3(this, Out, Left, Right, Alpha); }
unsigned long FVectorTypedChannel::GetType() { return TypedGetType2(this); }
unsigned long FVectorTypedChannel::CopySampled(float* Out) { return TypedCopySampled12(this, Out); }
float* FVectorTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0

// Type 4 (0662912c): 2 LAB_0661b880, 3 LAB_0661b950, 5 LAB_0661b940, 6 LAB_066121b0,
// 7 FUN_0661adc0 (the base's), 11 LAB_066121a0; the rest as the base.
unsigned long FQuatTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
unsigned long FQuatTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return TypedSlerpType4(this, Out, Left, Right, Alpha); }
unsigned long FQuatTypedChannel::GetType() { return TypedGetType4(this); }
unsigned long FQuatTypedChannel::CopySampled(float* Out) { return TypedCopySampled16(this, Out); }
float* FQuatTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0

unsigned long FPosQuatTypedChannel::Interpolate(float* Out, const float* Left, const float* Right, double Alpha) { return TypedLerpPosQuatScaled(this, Out, Left, Right, Alpha); }
unsigned long FPosQuatTypedChannel::BindHeader(unsigned long* Header) { return TypedBindType6(this, Header); }
unsigned long FPosQuatTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyPositionQuaternionSample(this, Left, Right, Alpha); }
unsigned long FPosQuatTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return TypedLerpPosQuat(this, Out, Left, Right, Alpha); }
unsigned long FPosQuatTypedChannel::GetType() { return TypedGetType6(this); }
unsigned long FPosQuatTypedChannel::CopySampled(float* Out) { return TypedCopyApplied(this, Out); }
unsigned long FPosQuatTypedChannel::SetOverlayPos(const float* Pos) { return SetChannelOverlayPosition(this, Pos); }
unsigned long FPosQuatTypedChannel::SetOverlayExtra(const float* Extra) { return SetChannelOverlayOrientation(this, Extra); }
unsigned long FPosQuatTypedChannel::AddOverlayPos(const float* Delta) { return TypedAddDelta(this, Delta); }
float* FPosQuatTypedChannel::GetSampled() { return TypedAppliedPtr(this); }
unsigned long FPosQuatTypedChannel::TouchMapped(void* ArgA, void* ArgB) { return TypedSampleXformType6(this, (float*)ArgA, (float*)ArgB); }
unsigned long FPosQuatTypedChannel::UpdateDeltaRate(float Dt) { return TypedStepDeltaType6(this, Dt); } // formerly Slot13
unsigned long FPosQuatTypedChannel::Step(float* Out, float Dt) { return TypedStepPosType6(this, Out, Dt); }

// ---- Typed-channel construction (retail operator new + ctor chain) ----------------------------
static void TypedResetPlayState(FTypedChannel* This)
{
    This->Time = 0.0f;
    This->LoopFlags = 0;
    This->Weight = 1.0f;
    This->Scale = 1.0f;
    This->PlayFlags &= 0xFFFFFFF8u;
    This->Field_0x30 = 1.0f;
}

// FUN_0661ad50 thiscall RET 4 (Owner), 0661ad50..7d: +4 Owner, +8 Kind 2, +0x14 -1,
// +0xC / +0x10 = 0, +0x24 Archetype = 0, the type-8 table (PTR_LAB_066294ac), FUN_0661acd0.
// Every other member keeps what the allocation held.
FTypedChannel::FTypedChannel(IChannelOwner* NewOwner)
{
    Owner = NewOwner;
    Kind = 2;
    Field14 = 0xFFFFFFFF;
    Field0C = 0;
    Field10 = 0;
    Archetype = nullptr;
    TypedResetPlayState(this);
}

// FUN_0661acf0 thiscall RET 4 (Archetype), 0661acf0..2d: as FUN_0661ad50 with Owner = 0 and
// +0x24 = Archetype, AddRef'd when non-null (0661ad15..1a), before the table and FUN_0661acd0.
FTypedChannel::FTypedChannel(FChannelArchetypeObject* NewArchetype)
{
    Owner = nullptr;
    Kind = 2;
    Field14 = 0xFFFFFFFF;
    Field0C = 0;
    Field10 = 0;
    Archetype = NewArchetype;
    if (NewArchetype != nullptr)
        NewArchetype->AddRef();
    TypedResetPlayState(this);
}

// The class tails: type 1 stores -1 at +0x40 (CreateTyped 06615998, CreateFromObject 06615b55,
// FUN_06611f90 06612080) before its table goes in; types 2 / 4 / 6 only install their tables.
FScalarTypedChannel::FScalarTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) { JointType = 0xFFFFFFFF; }
FScalarTypedChannel::FScalarTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) { JointType = 0xFFFFFFFF; }
FVectorTypedChannel::FVectorTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) {}
FVectorTypedChannel::FVectorTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) {}
FQuatTypedChannel::FQuatTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) {}
FQuatTypedChannel::FQuatTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) {}
FPosQuatTypedChannel::FPosQuatTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) {}
FPosQuatTypedChannel::FPosQuatTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) {}

// The switch every creation site runs on the type (1 / 2 / 4 / 6 / 8): operator new at the
static FTypedChannel* NewTypedChannelWithOwner(unsigned long Type, IChannelOwner* Owner)
{
    if (Type == 1)
        return new FScalarTypedChannel(Owner);
    if (Type == 2)
        return new FVectorTypedChannel(Owner);
    if (Type == 4)
        return new FQuatTypedChannel(Owner);
    if (Type == 6)
        return new FPosQuatTypedChannel(Owner);
    if (Type == 8)
        return new FTypedChannel(Owner);
    return nullptr;
}

static FTypedChannel* NewTypedChannelWithArchetype(unsigned long Type, FChannelArchetypeObject* Archetype)
{
    if (Type == 1)
        return new FScalarTypedChannel(Archetype);
    if (Type == 2)
        return new FVectorTypedChannel(Archetype);
    if (Type == 4)
        return new FQuatTypedChannel(Archetype);
    if (Type == 6)
        return new FPosQuatTypedChannel(Archetype);
    if (Type == 8)
        return new FTypedChannel(Archetype);
    return nullptr;
}

// FUN_06612a00 thiscall RET 0 (06612a00..62): for every slot of the record's vector, a
static void FreePlaybackTyped(FAnimPlayback* Playback)
{
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        if (Typed != nullptr)
        {
            if (Typed->Archetype != nullptr)
            {
                Typed->Archetype->Release();
                Typed->Archetype = nullptr;
            }
            ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.
        }
        ++Cursor;
    }
    Playback->Typed.clear();
    Playback->LongestTyped = nullptr;
}

// FUN_06611f90 thiscall RET 4 (NameEntry), 06611f90..06612133. record +0 = the entry
static void ClonePlaybackTypedChannels(FAnimPlayback* Playback, FAnimNameEntry* Entry)
{
    Playback->Name = Entry;
    Playback->Typed.resize(Entry->Tracks.size(), nullptr);
    std::vector<FTypedChannel*>::iterator Slot = Playback->Typed.begin();
    std::vector<FAnimTrack>::iterator Cursor = Entry->Tracks.begin();
    while (Cursor != Entry->Tracks.end())
    {
        unsigned long Type = 0;
        Cursor->Channel->GetFlags(&Type);
        FTypedChannel* Typed = NewTypedChannelWithArchetype(Type, Cursor->Channel);
        Typed->PlayState &= 0xFE;
        *Slot = Typed;
        ++Slot;
        ++Cursor;
    }
}

// FUN_06612250 thiscall RET 4 (Filter), 06612250..96: answer 1 up front (06612259); for every
static unsigned long PlaybackFilterTyped(FAnimPlayback* Playback, IChannel* Filter)
{
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        const unsigned long Type = Typed->GetType();
        if (Type != 8)
        {
            if (Filter->InMixer((unsigned long)((size_t)Typed)) != 0)
                return 0;
        }
        ++Cursor;
    }
    return 1;
}

// FUN_066122a0 thiscall RET 4 (Object), 066122a0..c6: 1 when the vector holds exactly that
// pointer (066122b1..b3), else 0 (also for an empty vector). Sole XREF LAB_066150a0 066150c3.
static unsigned long PlaybackHasTyped(FAnimPlayback* Playback, void* Object)
{
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        if (*Cursor == Object)
            return 1;
        ++Cursor;
    }
    return 0;
}

// FUN_066122d0 thiscall RET 8 (ArgA, ArgB), 066122d0..12: the first channel whose typed slot
static unsigned long PlaybackTouchMappedTyped(FAnimPlayback* Playback, void* ArgA, void* ArgB)
{
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        const unsigned long Type = Typed->GetType();
        if ((Type & 2) != 0)
        {
            Typed->TouchMapped(ArgA, ArgB);
            return 1;
        }
        ++Cursor;
    }
    return 0;
}

// The shared tail of IChannel::CreateTyped (06615a73..06615adc) and CreateFromObject
static unsigned long ChannelBindAndInsert(FAnimationObject* Animation, FTypedChannel* Typed,
                                          void* BindA, void* BindB, unsigned char bFreeOnRefusal)
{
    const unsigned long bBound =
        Typed->BindMap((unsigned long*)BindA, BindB);
    if ((bBound & 0xFF) == 0)
    {
        if (bFreeOnRefusal != 0)                                 // 06615aab..06615ad1
        {
            if (Typed->Archetype != nullptr)
            {
                Typed->Archetype->Release();
                Typed->Archetype = nullptr;
            }
            ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.        // FUN_066281d0
        }
        return 0xFFFFFFFF;
    }
    Animation->Channels->insert(Typed);
    return (unsigned long)((size_t)Typed);
}

// LAB_06615950 / IChannel+0x0C RET 0x14 (Owner, Type, BindA, BindB). Owner null -> -1
static unsigned long DACOM_API ChannelCreateTyped(void* ChannelThis, void* Owner, unsigned long Type,
                                                void* BindA, void* BindB)
{
    if (Owner == nullptr)
        return 0xFFFFFFFF;
    if (!IsTypedChannelType(Type))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileAnimComponent, 0x44D, "FATAL", "Unknown data type!\n");
        // Retail's invalid-type continuation reinterprets the owner as a channel.
        const unsigned long bBound =
            ((FTypedChannel*)Owner)->BindMap((unsigned long*)BindA, BindB); // 06615a81
        if ((bBound & 0xFF) == 0)
        {
            IDAComponent** Held =
                (IDAComponent**)((unsigned char*)Owner + 0x24);
            if (*Held != nullptr)                                // 06615ab5..c5
            {
                (*Held)->Release();
                *Held = nullptr;
            }
            ::operator delete(Owner);                            // 06615acc FUN_066281d0
            return 0xFFFFFFFF;
        }
        AnimationFromNested(ChannelThis, 8)->Channels->insert((FTypedChannel*)Owner);
        return (unsigned long)((size_t)Owner);
    }
    // 0661597a..06615a40: operator new at the type's size, FUN_0661ad50 with the owner, the
    // class tail (type 1: +0x40 = EBX = -1, OR EBX,-1 at 0661595a).
    FTypedChannel* Typed = NewTypedChannelWithOwner(Type, (IChannelOwner*)Owner);
    return ChannelBindAndInsert(AnimationFromNested(ChannelThis, 8), Typed, BindA, BindB, 1);
}

// LAB_06615b00 / IChannel+0x10 RET 0x10 (Object, BindA, BindB): the type is the archetype's
static unsigned long DACOM_API ChannelCreateFromObject(void* ChannelThis, void* Object, void* BindA,
                                                     void* BindB)
{
    if (Object == nullptr)
        return 0xFFFFFFFF;
    FChannelArchetypeObject* Archetype =
        (FChannelArchetypeObject*)((IChannelArchetype*)Object);
    unsigned long Type = 0;
    Archetype->GetFlags(&Type);
    if (!IsTypedChannelType(Type))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileAnimComponent, 0x479, "FATAL", "Unknown data type!\n");
        return 0xFFFFFFFF;
    }
    // 06615b37..06615c05: operator new at the type's size, FUN_0661acf0 (stores and AddRefs the
    // archetype, 0661ad12..1a), the class tail (type 1: +0x40 = EBX = -1, OR EBX,-1 at 06615b09).
    FTypedChannel* Typed = NewTypedChannelWithArchetype(Type, Archetype);
    return ChannelBindAndInsert(AnimationFromNested(ChannelThis, 8), Typed, BindA, BindB, 0);
}

// FUN_06615e10 / IChannel+0x14 RET 8: set lookup; FUN_06615ec0 (leave the mixer); release the
// archetype (vtbl+8) and zero the field; free the channel; FUN_06617c90 erase; 0. Key -1 or
// a miss -> -1.
static unsigned long DACOM_API ChannelDestroy(void* ChannelThis, unsigned long Key)
{
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0xFFFFFFFF;
    MixerUnbindTyped(Animation, Typed);                          // 06615e6e..75
    if (Typed->Archetype != nullptr)                             // 06615e83..90
    {
        Typed->Archetype->Release();
        Typed->Archetype = nullptr;
    }
    ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.                // 06615e93..94 FUN_066281d0
    Animation->Channels->erase(Typed);                           // 06615e9c..a4
    return 0;
}

// LAB_06615d80 / IChannel+0x18 RET 0xC: *Out = the channel's archetype (+0x24), AddRef'd when
// non-null; 0. Key -1 or a miss -> -1 with Out untouched.
static unsigned long DACOM_API ChannelGetObject(void* ChannelThis, unsigned long Key, void** Out)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0xFFFFFFFF;
    *Out = (IChannelArchetype*)(Typed->Archetype);
    if (Typed->Archetype != nullptr)
        Typed->Archetype->AddRef();
    return 0;
}

// FUN_06616640 / IChannel+0x2C RET 0xC: the dword goes to typed +0x2C (Scale). Key -1 or a
// miss writes nothing.
static GENRESULT DACOM_API ChannelSetUser(void* ChannelThis, unsigned long Key, unsigned long User)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    std::memcpy(&Typed->Scale, &User, sizeof(Typed->Scale));
    return GR_OK;
}

// FUN_066166a0 / IChannel+0x30 RET 8: FLD typed +0x2C (Scale); key -1 or a miss -> 0.0.
static float DACOM_API ChannelGetUser(void* ChannelThis, unsigned long Key)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0.0f;
    return Typed->Scale;
}

// FUN_06616ac0 / IChannel+0x48 RET 0xC: *Out = !(Weight < 0) -- FCOMP qword 0.0 on typed
// +0x28, unordered counts as forward -- and 0. Key -1 or a miss -> -2, Out untouched.
static GENRESULT DACOM_API ChannelGetMapped(void* ChannelThis, unsigned long Key, unsigned char* Out)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return (GENRESULT)0xFFFFFFFE;
    *Out = Typed->Weight < 0.0f ? 0 : 1;
    return GR_OK;
}

// LAB_06616550 / IChannel+0x24 RET 8: FLD typed +0x1C (the channel's current time, despite
// the slot's name); key -1 or a miss -> -1.0f (0661655f / 066165b8).
static float DACOM_API ChannelGetDuration(void* ChannelThis, unsigned long Key)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return -1.0f;
    return Typed->Time;
}

// LAB_066169b0 / IChannel+0x40 RET 0xC: Out always receives 16 bytes -- the channel's bind
static unsigned long* DACOM_API ChannelGetHeader(void* ChannelThis, unsigned long* Out, unsigned long Key)
{
    unsigned long Default[4];
    Default[0] = 2;
    Default[1] = 0;
    Default[2] = 0;
    Default[3] = 0xFFFFFFFF;
    const unsigned long* Source = Default;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed != nullptr)
        Source = (const unsigned long*)(TypedBindDesc(Typed));
    std::memcpy(Out, Source, sizeof(Default));
    return Out;
}

// FUN_066164d0 / IChannel+0x20 RET 8: a playing channel (PlayState bit 0) is posed where it
// stands (FUN_0661b070), stops playing, and the call answers 1. Otherwise 0.
static unsigned long DACOM_API ChannelStopGraph(void* ChannelThis, unsigned long Key)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr || (Typed->PlayState & 1) == 0)         // 06616527..2b
        return 0;
    TypedApplyFromArchetype(Typed);                              // 0661652d
    Typed->PlayState &= 0xFE;                                    // 06616539
    return 1;
}

// FUN_066165d0 / IChannel+0x28 RET 0xC: typed slot 0x10 (SetTime) with the float, answering
// what it answers (06616625..36); key -1 or a miss -> 0.
static unsigned long DACOM_API ChannelBlend(void* ChannelThis, unsigned long Key, float Duration)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0;
    return Typed->SetTime(Duration);
}

// FUN_06616720 / IChannel+0x34 RET 0xC: typed slot 0x20 with the vector and nothing else --
// unlike IAnimation2's playback setter (FUN_06612a70), no mixer overlay is touched here.
static GENRESULT DACOM_API ChannelSetOverlayPos(void* ChannelThis, unsigned long Key, float* Pos)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->SetOverlayPos(Pos);
    return GR_OK;
}

// FUN_06616790 / IChannel+0x38 RET 0xC: typed slot 0x24 only (066167e2..ee).
static GENRESULT DACOM_API ChannelSetOverlayExtra(void* ChannelThis, unsigned long Key, float* Extra)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->SetOverlayExtra(Extra);
    return GR_OK;
}

// FUN_066168f0 / IChannel+0x3C RET 8: 1 when the channel is a leaf of its mixer list.
static unsigned long DACOM_API ChannelInMixer(void* ChannelThis, unsigned long Key)
{
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0;
    return MixerHasLeaf(Animation, Typed);
}

// FUN_06616a50 / IChannel+0x44 RET 0xC: typed slot 0x28 only.
static GENRESULT DACOM_API ChannelAddOverlayPos(void* ChannelThis, unsigned long Key, float* Delta)
{
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->AddOverlayPos(Delta);
    return GR_OK;
}

// FUN_06627f40 / 06627f50 / 06627f60: replace `this` with the controlling unknown at +0x34 and
// tail-jump through its vtable slot 0 / 1 / 2. No checks.
static GENRESULT DACOM_API HardpointQueryInterface(FHardpointObject* This, const char* Iid,
                                                  void** Out)
{
    return This->Controller->QueryInterface(Iid, Out);
}

static unsigned long DACOM_API HardpointAddRef(FHardpointObject* This)
{
    return This->Controller->AddRef();
}

static unsigned long DACOM_API HardpointRelease(FHardpointObject* This)
{
    return This->Controller->Release();
}

static FHardpointObject* HardpointFromNested(void* Nested, unsigned long Offset)
{
    return (FHardpointObject*)((unsigned char*)Nested - Offset);
}

static GENRESULT DACOM_API HardpointEngineComponentQueryInterface(void* Nested, const char* Iid,
                                                                 void** Out)
{
    return HardpointQueryInterface(HardpointFromNested(Nested, 4), Iid, Out);
}

static unsigned long DACOM_API HardpointEngineComponentAddRef(void* Nested)
{
    return HardpointAddRef(HardpointFromNested(Nested, 4));
}

static unsigned long DACOM_API HardpointEngineComponentRelease(void* Nested)
{
    return HardpointRelease(HardpointFromNested(Nested, 4));
}

// LAB_0661e990, RET 4: INC +0x1c and return it. No check (until 2026-09-30 the port returned 0
// for a null This).
static unsigned long DACOM_API ChannelArchetypeAddRef(FChannelArchetypeObject* This)
{
    This->RefCount += 1;
    return This->RefCount;
}

// DAT_0662b10c: live ChannelArchetype count (INC in FUN_0661d730, DEC in FUN_0661d750).
static unsigned long G_ChannelArchetypeLive = 0;
// Shared compressed-frame scratch; freed with the last channel archetype.
static unsigned char* G_ChannelFramePool = nullptr;
static unsigned long G_ChannelFramePoolSize = 0;

// FUN_0661d730, thiscall: zero +0x10 / +0x14 / +0x18, vtable PTR_LAB_0662952c, INC
// DAT_0662b10c. No check (until 2026-09-30 the port returned for a null This).
static void ChannelArchetypeInnerCtor(FChannelArchetypeObject* This)
{
    This->Frames = nullptr;
    This->Stride = 0;
    This->Duration = 0.0f;
    G_ChannelArchetypeLive += 1;
}

// FUN_0661d750, thiscall: vtable PTR_LAB_0662952c; free +0x10 when non-null; DEC DAT_0662b10c
static void ChannelArchetypeTeardown(FChannelArchetypeObject* This)
{
    if (This->Frames != nullptr)
        std::free(This->Frames);
    G_ChannelArchetypeLive -= 1;
    if (G_ChannelArchetypeLive == 0 && G_ChannelFramePool != nullptr)
    {
        std::free(G_ChannelFramePool);
        G_ChannelFramePool = nullptr;
        G_ChannelFramePoolSize = 0;
    }
}

// FUN_0661e9a0, RET 4: DEC +0x1c when above 0; at 0 set it to 1, FUN_0661d750, operator
// delete, return 0; else return the count. No check (the port used to test This).
static unsigned long DACOM_API ChannelArchetypeRelease(FChannelArchetypeObject* This)
{
    if (This->RefCount != 0)
        This->RefCount -= 1;
    if (This->RefCount == 0)
    {
        This->RefCount = 1;
        ChannelArchetypeTeardown(This);
        std::free(This);
        return 0;
    }
    return This->RefCount;
}

// LAB_0661e8e0, RET 0xC: walks the {iid, offset} table at 066295a4 -- one entry,
static GENRESULT DACOM_API ChannelArchetypeQueryInterface(FChannelArchetypeObject* This,
                                                         const char* Iid, void** Out)
{
    if (std::strcmp(Iids::ChannelArchetype, Iid) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    This->AddRef();
    *Out = This;
    return GR_OK;
}

// ---- Fixed-size free lists (see FEngBaseFreeList in EngBase.h) ----
// Element sizes 0x38 / 0xEC / 0x48 (static_asserts above), limit 0x40 and the empty list are
// the values LAB_0661f6a0 / LAB_0661f6e0 / LAB_0661f720 store.
static FEngBaseFreeList G_ArchetypeHandlePool = {sizeof(FArchetypeHandle), nullptr, 0x40, 0}; // DAT_0662b168
static FEngBaseFreeList G_InstancePool = {sizeof(FEngineInstance), nullptr, 0x40, 0};         // DAT_0662b158
static FEngBaseFreeList G_TransformPool = {sizeof(FInstanceTransform), nullptr, 0x40, 0};     // DAT_0662b114

// FUN_06625bd0 (thiscall): pop the free-list head (its first dword is the next link) and
// decrement the count; an empty list falls back to malloc(ElementSize). The memory is not
// cleared.
static void* FreeListAlloc(FEngBaseFreeList* List)
{
    void* Element = List->FreeHead;
    if (Element != nullptr)
    {
        List->FreeHead = *(void**)Element;
        List->FreeCount -= 1;
        return Element;
    }
    return std::malloc(List->ElementSize);
}

// FUN_06625c00 (thiscall, RET 4): 06625c06 CMP limit,count / JG push; 06625c0a TEST limit /
// JL push -- a limit above the count, or a negative limit, keeps the element on the list
// (count + 1, link through the first dword); otherwise free().
static void FreeListFree(FEngBaseFreeList* List, void* Element)
{
    if (List->FreeLimit > List->FreeCount || List->FreeLimit < 0)
    {
        List->FreeCount += 1;
        *(void**)Element = List->FreeHead;
        List->FreeHead = Element;
        return;
    }
    std::free(Element);
}

// FUN_06625c40 (thiscall): free every cached element, then count = 0 and head = 0. Live
// elements are not tracked and are not touched.
static void FreeListDrain(FEngBaseFreeList* List)
{
    void* Element = List->FreeHead;
    while (Element != nullptr)
    {
        void* Next = *(void**)Element;
        std::free(Element);
        Element = Next;
    }
    List->FreeCount = 0;
    List->FreeHead = nullptr;
}

// The list sentinel the ctor FUN_0661f550 builds for +0x48 and +0x54: operator new(0x3c)
// linked to itself (0661f5fc..0661f60a, 0661f617..0661f61e). No null test.
static FEngineChildNode* CreateListSentinel()
{
    FEngineChildNode* Sentinel =
        new FEngineChildNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    return Sentinel;
}

// FUN_066254d0 with the flag clear: the node value's dtor zeroes name[0] and the +0x38 pointer.
static void DestroyChildNodeValue(FEngineChildNode* Node)
{
    Node->Name[0] = '\0';
    Node->Object = nullptr;
}

// FUN_06623620 (list erase(first, last), RET 0xC): unlink each node, zero name[0] and +0x38,
// operator delete, count - 1.
static void EraseChildNodes(FEngineChildNode* First, FEngineChildNode* Last, unsigned long* Count)
{
    FEngineChildNode* Node = First;
    while (Node != Last)
    {
        FEngineChildNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        *Count -= 1;
        Node = Next;
    }
}

// FUN_066235d0 (list insert(where, value), RET 0xC): operator new(0x3c), link before Where
// (Prev = Where->Prev, or the node itself when that is null), copy the 0x34-byte value (MOVSD
// x 0xD), count + 1.
static void InsertChildNode(FEngineChildNode* Where, unsigned long* Count, const char* Name,
                            IDAComponent* Object)
{
    FEngineChildNode* Node = new FEngineChildNode;
    Node->Next = Where;
    Node->Prev = Where->Prev != nullptr ? Where->Prev : Node;
    Where->Prev = Node;
    Node->Prev->Next = Node;
    std::memcpy(Node->Name, Name, sizeof(Node->Name));
    Node->Object = Object;
    *Count += 1;
}

// FUN_0661f550 (thiscall), in retail order: SearchPath (+4) = 0; instance-root map (+0x10,
static void EngineConstruct(FEngineObject* This)
{
    new (This) FEngineObject();
    This->SearchPath = nullptr;
    This->InstanceLookupCount = 0;
    This->InstanceCacheHits = 0;
    This->bInstanceCacheValid = 0;
    This->ComponentList = CreateListSentinel();
    This->ComponentCount = 0;
    This->EngineComponentList = CreateListSentinel();
    This->EngineComponentCount = 0;
    This->bInitialized = 0;
}

// FUN_0661f750 (thiscall), in retail order:
static void EngineDestruct(FEngineObject* This)
{
    FreeListDrain(&G_ArchetypeHandlePool);
    FreeListDrain(&G_InstancePool);
    FreeListDrain(&G_TransformPool);
    if (This->SearchPath != nullptr)
    {
        This->SearchPath->Release();
        This->SearchPath = nullptr;
    }
    This->bInstanceCacheValid = 0;
    This->Instances.clear();
    This->Archetypes.clear();

    FEngineChildNode* Sentinel = This->ComponentList;
    FEngineChildNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        Node->Object->Release();
        FEngineChildNode* Next = Node->Next;
        Node->Prev->Next = Next;
        Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        This->ComponentCount -= 1;
        Node = Next;
    }

    EraseChildNodes(This->EngineComponentList->Next, This->EngineComponentList,
                    &This->EngineComponentCount);
    This->bInitialized = 0;
    EraseChildNodes(This->EngineComponentList->Next, This->EngineComponentList,
                    &This->EngineComponentCount);
    delete This->EngineComponentList;
    This->EngineComponentList = nullptr;
    This->EngineComponentCount = 0;
    EraseChildNodes(This->ComponentList->Next, This->ComponentList, &This->ComponentCount);
    delete This->ComponentList;
    This->ComponentList = nullptr;
    This->ComponentCount = 0;
    This->~FEngineObject();
}

// LAB_06625460 (RET 4): INC [this+0x60], return it.
static unsigned long DACOM_API EngineAddRef(FEngineObject* This)
{
    This->RefCount += 1;
    return This->RefCount;
}

// FUN_06625470 (RET 4): 06625478 TEST / JBE -- decrement only while above 0; at 0 the count
// is pinned to 1, FUN_0661f750 runs, the block is freed and 0 returned; otherwise the count.
static unsigned long DACOM_API EngineRelease(FEngineObject* This)
{
    if (This->RefCount > 0)
        This->RefCount -= 1;
    if (This->RefCount != 0)
        return This->RefCount;
    This->RefCount = 1;
    EngineDestruct(This);
    std::free(This);
    return 0;
}

// LAB_06621ca0 (RET 0xC): the {iid, offset} table at 06629608 -- {"1.11_IEngine", 0},
static GENRESULT DACOM_API EngineQueryInterface(FEngineObject* This, const char* Iid, void** Out)
{
    if (std::strcmp(Iids::Engine, Iid) == 0 || std::strcmp(Iids::Engine2, Iid) == 0)
    {
        This->AddRef();
        *Out = This;
        return GR_OK;
    }

    *Out = nullptr;
    FEngineChildNode* Sentinel = This->ComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Node->Object->QueryInterface(Iid, Out) >= 0)
            return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// The smart-pointer assignment into +4 that FUN_0661fee0, LAB_0661fdf0 (0661fe97..ce) and
// LAB_0661ffc0 (06620052..77) inline: equal -> nothing; else Release the old one when set
// (+4 = 0), store the new one and AddRef it when non-null.
static void ReplaceSearchPath(FEngineObject* This, IDAComponent* NewPath)
{
    if (This->SearchPath == NewPath)
        return;
    if (This->SearchPath != nullptr)
    {
        This->SearchPath->Release();
        This->SearchPath = nullptr;
    }
    This->SearchPath = NewPath;
    if (NewPath != nullptr)
        NewPath->AddRef();
}

// Retail strtok on DAT_0662a524 = " \t". First token = name; token starting with '=' then next = value.
static void ParseEngineProfileLine(char* Line, char** NameOut, const char** ValueOut)
{
    *NameOut = nullptr;
    *ValueOut = nullptr;
    char* Name = nullptr;
    const char* Value = nullptr;
    unsigned char bSawEquals = 0;
    char* Token = std::strtok(Line, " \t");
    while (Token != nullptr)
    {
        if (Token[0] == ';')
            break;
        if (Name == nullptr)
            Name = Token;
        else if (bSawEquals != 0)
            Value = Token;
        else if (Token[0] == '=')
            bSawEquals = 1;
        Token = std::strtok(nullptr, " \t");
    }
    if (Name != nullptr)
    {
        char* Space = std::strchr(Name, ' ');
        if (Space != nullptr)
            *Space = '\0';
        char* Tab = std::strchr(Name, '\t');
        if (Tab != nullptr)
            *Tab = '\0';
    }
    *NameOut = Name;
    *ValueOut = Value;
}

// LAB_0661f930 / IEngine +0x0C (RET 8). The engine's own refcount is never touched here.
static GENRESULT DACOM_API EngineLoadEngineComponents(FEngineObject* This, IDAComponent* SystemContainer)
{
    if (This->bInitialized != 0)
        return GR_OK;

    if (SystemContainer == nullptr)
    {
        EngBaseFdump(kFdumpNotice, G_FdumpFileEngine, 0x1A3, "NOTICE",
                     "Engine: load_engine_components: system parameter is NULL, continuing with "
                     "complete faith in the client...");
    }

    ICOManager* Dacom = DACOM_Acquire();
    IProfileParser* Parser = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) < 0)
    {
        EngBaseFdump(kFdumpError, G_FdumpFileEngine, 0x1AF, "ERROR",
                     "Engine: load_engine_components: unable to acquire IID_IProfileParser");
        if (Parser != nullptr)
            Parser->Release();
        return GR_GENERIC;
    }

    void* Section = Parser->OpenSection(G_NameEngine, 3);
    if (Section == nullptr)
    {
        This->bInitialized = 1;
        if (Parser != nullptr)
            Parser->Release();
        return GR_OK;
    }

    char Line[0x100];
    char NodeName[0x30];
    int LineIndex = 0;
    while (Parser->ReadLine(Section, LineIndex++, Line, sizeof(Line)) != 0)
    {
        char* Name = nullptr;
        const char* Value = nullptr;
        ParseEngineProfileLine(Line, &Name, &Value);
        if (Name == nullptr)
            continue;

        void* OuterInner = nullptr;
        void* Created = nullptr;
        FAggDesc18 Desc;
        Desc.Size = G_AggDescSizeAnimation;
        Desc.Name = Name;
        Desc.Inner = This;
        Desc.Outer = &OuterInner;
        Desc.Value = Value;
        Desc.SystemContainer = SystemContainer;

        const char* Status = G_StatusFailed;
        if (Dacom->CreateInstance((AGGDESC*)(&Desc), &Created) >= 0)
        {
            Status = G_StatusOk;
            BuildNodeName(NodeName, Name);
            InsertChildNode(This->ComponentList, &This->ComponentCount, NodeName,
                            (IDAComponent*)OuterInner);
        }

        char Notice[0x1FF0];
        std::sprintf(Notice, "ENGINE::load_engine_components: aggregation of '%s' [%s] returned %s\n",
                     Name, Value != nullptr ? Value : "", Status);
        EngBaseFdump(kFdumpNotice, G_FdumpFileEngine, 0x1FC, "NOTICE", Notice);
    }

    // Pass 2. Aggregate is the smart pointer at [ESP+0x14]: released before every QI and once
    // after the walk.
    FEngineChildNode* Sentinel = This->ComponentList;
    IAggregateComponent* Aggregate = nullptr;
    FEngineChildNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        if (Aggregate != nullptr)
        {
            Aggregate->Release();
            Aggregate = nullptr;
        }
        if (Node->Object->QueryInterface(Iids::AggregateComponent,
                                         (void**)(&Aggregate)) >= 0 &&
            Aggregate->Initialize() >= 0)
        {
            Node = Node->Next;
            continue;
        }

        char Warning[0x1FF0];
        std::sprintf(Warning,
                     "ENGINE::load_engine_components: initialization of '%s' failed, removing "
                     "from aggregate\n",
                     Node->Name);
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x224, "WARNING", Warning);
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        This->ComponentCount -= 1;
        Node = This->ComponentList->Next;
    }
    if (Aggregate != nullptr)
    {
        Aggregate->Release();
        Aggregate = nullptr;
    }

    // Pass 3.
    Sentinel = This->ComponentList;
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        IDAComponent* EngineComponent = nullptr;
        if (Node->Object->QueryInterface(Iids::EngineComponent,
                                         (void**)(&EngineComponent)) < 0)
            continue;
        EngineComponent->Release();
        BuildNodeName(NodeName, Node->Name);
        InsertChildNode(This->EngineComponentList, &This->EngineComponentCount, NodeName,
                        EngineComponent);
    }

    This->bInitialized = 1;
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}

// LAB_0661fdf0 / IEngine +0x18 (RET 8). Created is the smart pointer at [ESP+4] (0 first).
static GENRESULT DACOM_API EngineSetSearchPath(FEngineObject* This, const char* Path)
{
    IDAComponent* Created = nullptr;
    if (Path == nullptr)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x270, "WARNING",
                     "Engine: set_search_path: path is NULL");
    }
    else
    {
        FAggDesc8 Desc;
        Desc.Size = G_AggDescSizeSearchPath;
        Desc.Name = G_NameSearchPath;
        if (DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc),
                                            (void**)(&Created)) >= 0)
        {
            ((ISearchPath*)((void*)Created))->SetPath(Path);
            ReplaceSearchPath(This, Created);
            if (Created != nullptr)
                return (GENRESULT)(Created->Release());
            return GR_OK;
        }
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x275, "WARNING",
                     "Engine: set_search_path: unable to create search path");
    }
    if (Created != nullptr)
        return (GENRESULT)(Created->Release());
    return GR_OK;
}

// FUN_0661fee0 / IEngine +0x20 (RET 8): the smart-pointer assignment into +4. EAX is whatever
// the last call left (AddRef / Release) or the old pointer; the port returns 0.
static GENRESULT DACOM_API EngineSetFilesystem(FEngineObject* This, IDAComponent* NewPath)
{
    ReplaceSearchPath(This, NewPath);
    return GR_OK;
}

// FUN_0661ff20 / IEngine +0x1C (RET 0xC). Local smart pointer = 0.
static GENRESULT DACOM_API EngineQuerySearchPath(FEngineObject* This, char* Dest, unsigned long MaxChars)
{
    ISearchPath* SearchPathIface = nullptr;
    if (This->SearchPath != nullptr &&
        This->SearchPath->QueryInterface(Iids::SearchPath,
                                         (void**)(&SearchPathIface)) >= 0)
    {
        const GENRESULT Result = SearchPathIface->GetPath(Dest, MaxChars);
        if (SearchPathIface != nullptr)
            SearchPathIface->Release();
        return Result;
    }
    *Dest = 0;
    if (SearchPathIface != nullptr)
        SearchPathIface->Release();
    return GR_OK;
}

static FArchetypeHandle* const G_InvalidArchetype =
    (FArchetypeHandle*)G_InvalidEngineHandle;

// The 0x34 FileSystem descriptor every caller builds inline (LAB_0661ffc0 0661ffde..06620025,
// FUN_066201b0, FUN_06611840 ...): zeroed, then size / "FileSystem" / +8 backend / +0xC path /
// 0x80000000 / 1 / 3 / 0x8000080 / +0x30 = -1.
static void FillFileSystemDesc(FEngBaseFileSystemDesc* Desc, const char* Path, const char* Backend)
{
    std::memset(Desc, 0, sizeof(*Desc));
    Desc->Size = 0x34;
    Desc->ComponentName = G_NameFileSystem;
    Desc->BackendName = Backend;
    Desc->DataPath = Path;
    Desc->Field10 = 0x80000000;
    Desc->Field14 = 1;
    Desc->Field1C = 3;
    Desc->Field20 = 0x8000080;
    Desc->Field30 = 0xFFFFFFFF;
}

// 0662002f..0662003d: Path[1] == ':' or Path[0] == '\\' && Path[1] == '\\'. No other test.
static bool IsAbsoluteFilePath(const char* Path)
{
    if (Path[1] == ':')
        return true;
    if (Path[0] == '\\' && Path[1] == '\\')
        return true;
    return false;
}


// FUN_0661f8b0 (thiscall, RET 4): Crc (+4 of the name wrapper) == 0 -> -1; otherwise
// find(Crc) in the map at +0x30 (lower_bound FUN_0661a260 and the unsigned JC test at
// 0661f8f3); miss -> -1, hit -> the node value (+0x10).
static FArchetypeHandle* LookupArchetype(FEngineObject* This, FArchetypeName* Name)
{
    if (Name->Crc == 0)
        return G_InvalidArchetype;
    FArchetypeMap::iterator It = This->Archetypes.find(Name->Crc);
    if (It == This->Archetypes.end())
        return G_InvalidArchetype;
    return It->second;
}

// FUN_0661ea00 (thiscall, RET 4) on a block from FUN_0661e9e0 (free list DAT_0662b168): the
FArchetypeHandle::FArchetypeHandle(const FArchetypeName* Source)
    : Name(Source->Text != nullptr ? Source->Text : "")
    , Crc(Source->Crc)
    , RefCount(0)
{
}

// FUN_0661eab0 (thiscall on the handle, RET 4): ref (+0x14) + 1, then every child of the
// vector +0x1C..+0x20 (stride 0x48) goes through the engine's vtable +0x38 (slot 14) with its
// +0x44 handle. No tests. Returns the new count.
static unsigned long ArchetypeHandleAddRef(FArchetypeHandle* Handle, FEngineObject* Engine)
{
    Handle->RefCount += 1;
    for (std::vector<FArchetypeChild>::iterator Child = Handle->Children.begin();
         Child != Handle->Children.end(); ++Child)
        Engine->AddRefHandle(Child->Handle);
    return Handle->RefCount;
}

// FUN_0661eaf0 (thiscall on the handle, RET 4): ref - 1 (no floor, 0661eafe DEC), then every
// child through vtable +0x3C (slot 15). Returns the new count.
static unsigned long ArchetypeHandleRelease(FArchetypeHandle* Handle, FEngineObject* Engine)
{
    Handle->RefCount -= 1;
    for (std::vector<FArchetypeChild>::iterator Child = Handle->Children.begin();
         Child != Handle->Children.end(); ++Child)
        Engine->ReleaseHandle(Child->Handle);
    return Handle->RefCount;
}

// LAB_066213b0 / IEngine +0x38 (RET 8): -1 or 0 -> return (EAX is not written on that path;
// the port returns 0); otherwise FUN_0661eab0.
static unsigned long DACOM_API EngineAddRefHandle(FEngineObject* This, FArchetypeHandle* Handle)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    return ArchetypeHandleAddRef(Handle, This);
}

// LAB_066213d0 / IEngine +0x3C (RET 8):
static unsigned long DACOM_API EngineReleaseHandle(FEngineObject* This, FArchetypeHandle* Handle)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    const unsigned long Count = ArchetypeHandleRelease(Handle, This);
    if (Count != 0)
        return Count;

    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DestroyArchetype(Handle);

    if (This->Archetypes.erase(Handle->Crc) == 0)
    {
        char Message[0x2000];
        std::sprintf(Message, "Failed to find %s in archetype name map\n", Handle->Name.c_str());
        EngBaseFdump(kFdumpError, G_FdumpFileEngine, 0x47E, "ERROR", Message);
    }

    // 066214bc..0662150d: the inlined ~FArchetypeHandle -- constraint vector, child vector,
    // then the string (members in reverse order); 06621510 FUN_0661e9f0 returns the block.
    Handle->~FArchetypeHandle();
    FreeListFree(&G_ArchetypeHandlePool, Handle);
    return 0;
}

static void CallEngineSlot14(FEngineObject* This, FArchetypeHandle* Handle)
{
    This->AddRefHandle(Handle);
}

// The Engine +0x58 walk of LAB_06621150 (0662127d..95): every node's +0x38 component gets
// vtable +0x10 (CreateArchetype) (handle, FileSystem). No checks (until 2026-09-30 the port
// tested the list head and skipped null components).
static void FanoutIecCreateArchetype(FEngineObject* This, FArchetypeHandle* Handle, void* FileSystem)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->CreateArchetype(Handle, FileSystem);
}

static void FanoutIecDuplicateArchetype(FEngineObject* This, FArchetypeHandle* NewHandle,
                                       FArchetypeHandle* OldHandle)
{
    // 06621387..9f: IEC +0x14 (new, old) on every node of +0x54, no tests.
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DuplicateArchetype(NewHandle, OldHandle);
}

// LAB_0661fda0 / IEngine +0x10 (RET 8): a null callback returns the +0x5C count at once;
// otherwise walk +0x54 calling cdecl callback(name @ +8, pointer @ +0x38) and stop when the
// returned AL is 0 (0661fdc7 TEST AL,AL); return the count.
static unsigned long DACOM_API EngineEnumerateEngineComponents(FEngineObject* This,
                                                              int(__cdecl* Callback)(const char*,
                                                                                    void*))
{
    if (Callback != nullptr)
    {
        FEngineChildNode* Sentinel = This->EngineComponentList;
        for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            if ((unsigned char)(Callback(Node->Name, Node->Object)) == 0)
                break;
        }
    }
    return This->EngineComponentCount;
}

static void EngineUpdateInstanceTree(FEngineObject* This, FEngineInstance* Instance,
                                    unsigned long Flags, void* Arg);

// FUN_06621de0 / IEngine+0x14 (stdcall RET 8). No checks.
static GENRESULT DACOM_API EngineNotifyEngineComponents(FEngineObject* This, void* Arg)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->Notify(Arg);
    for (FEngineInstanceMap::iterator It = This->Instances.begin(); It != This->Instances.end();
         ++It)
        EngineUpdateInstanceTree(This, It->second, 0, nullptr);
    return GR_OK;
}

// LAB_0661ff90 / IEngine+0x24 RET 8: *out = this+4 or DACOM_Acquire; AddRef.
// No checks: *Out = +4; null -> *Out = DACOM_Acquire(); AddRef through *Out and return that
// result.
static GENRESULT DACOM_API EngineGetSearchPath(FEngineObject* This, void** Out)
{
    *Out = This->SearchPath;
    if (*Out == nullptr)
        *Out = DACOM_Acquire();
    return (GENRESULT)(((IDAComponent*)(*Out))->AddRef());
}

// LAB_0661ffc0 / IEngine +0x28 (RET 0x10): Path or Out null -> -2. Descriptor 0x34
static GENRESULT DACOM_API EngineCreateFileSystem(FEngineObject* This, const char* Path, void** Out,
                                                 void* Factory)
{
    if (Path == nullptr || Out == nullptr)
        return GR_INVALID_PARMS;

    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Path, nullptr);

    // Every candidate is called through vtable +0xC (CreateInstance): a component factory,
    // the COManager, or whatever +4 holds.
    IComponentFactory* Creator = (IComponentFactory*)Factory;
    if (Creator == nullptr)
    {
        if (IsAbsoluteFilePath(Path))
        {
            Creator = (IComponentFactory*)((void*)(DACOM_Acquire()));
        }
        else
        {
            if (This->SearchPath == nullptr)
                ReplaceSearchPath(This, DACOM_Acquire());
            Creator = (IComponentFactory*)((void*)(This->SearchPath));
        }
    }
    return Creator->CreateInstance((AGGDESC*)(&Desc), Out);
}

// LAB_066200c0 / IEngine +0x2C (RET 8):
static FArchetypeHandle* DACOM_API EngineCreateCompoundArchetype(FEngineObject* This,
                                                                FArchetypeName* Name)
{
    DACOM_CRC::GetCRC32(nullptr);
    FArchetypeName Local;
    char Anonymous[0x100];
    if (Name->Crc != 0)
    {
        Local.Text = Name->Text;
        Local.Crc = Name->Crc;
    }
    else
    {
        Local.Text = Anonymous;
        do
        {
            wsprintfA(Anonymous, G_AnonymousFormat, G_AnonymousIndex++);
            Local.Crc = DACOM_CRC::GetCRC32(Anonymous);
        } while (LookupArchetype(This, &Local) != G_InvalidArchetype);
        Local.Crc = DACOM_CRC::GetCRC32(Anonymous);
    }

    void* Block = FreeListAlloc(&G_ArchetypeHandlePool);
    FArchetypeHandle* Handle = nullptr;
    if (Block != nullptr)
        Handle = new (Block) FArchetypeHandle(&Local);
    This->Archetypes.insert(FArchetypeMap::value_type(Local.Crc, Handle));
    This->AddRefHandle(Handle);
    return Handle;
}

// The child vector's resize(size + 1, element) in FUN_06620500 (06620536..d7 / 06620700..a3,
static FArchetypeChild* AppendArchetypeChild(FArchetypeHandle* Handle)
{
    const std::size_t Count = Handle->Children.size();
    Handle->Children.resize(Count + 1, FArchetypeChild());
    return Handle->Children.data() + Count;
}

// The resize(n) back to the size before a failed FUN_066201b0 (06620636..ab / 066207de..4c):
// the elements past n are erased (FUN_066230f0).
static void ShrinkArchetypeChildren(FArchetypeHandle* Handle, std::size_t Count)
{
    Handle->Children.erase(Handle->Children.begin() + Count, Handle->Children.end());
}

// The IFileSystem calls every EngBase site makes inline through the vtable, without testing
// the pointer or the arguments (until 2026-09-30 these wrappers returned 0 / -1 on a null
// FileSystem, and two of them -- +0x70 and +0x5C -- had no caller at all and were removed).
static int FileSystemOpenNode(void* FileSystem, const char* Name)
{
    return AsFileSystem(FileSystem)->OpenNode(Name); // +0x60
}

static int FileSystemFindFirst(void* FileSystem, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return AsFileSystem(FileSystem)->FindFirst(Pattern, // +0x48
                                            FindData);
}

static int FileSystemFindNext(void* FileSystem, int FindHandle, WIN32_FIND_DATAA* FindData)
{
    return AsFileSystem(FileSystem)->FindNext((unsigned long)FindHandle, // +0x4C
                                            FindData);
}

static void FileSystemFindClose(void* FileSystem, int FindHandle)
{
    AsFileSystem(FileSystem)->FindClose((unsigned long)FindHandle); // +0x50
}


static unsigned long ChannelStrideFromHeader(unsigned long Flags, float SampleRate)
{
    unsigned long Stride = 0;
    if ((Flags & G_ChannelFlagScalar) != 0)
        Stride = 4;
    if ((Flags & G_ChannelFlagPosition) != 0)
        Stride += 0xC;
    if ((Flags & G_ChannelFlagQuat) != 0)
        Stride += 0x10;
    if ((Flags & G_ChannelFlagScalarOnly) != 0)
        Stride = 4;
    if ((Flags & G_ChannelFlagZeroPos) != 0)
        Stride += 0;
    if ((Flags & G_ChannelFlagIdentQuat) != 0)
        Stride += 0;
    if ((Flags & G_ChannelFlagCompQuat3) != 0)
        Stride += 6;
    if ((Flags & G_ChannelFlagCompAxis) != 0)
        Stride += 6;
    if ((Flags & G_ChannelFlagCompQuat4) != 0)
        Stride += 8;
    if (SampleRate < 0.0f)
        Stride += 4;
    return Stride;
}

// FUN_0661d7a0, thiscall RET 0x4 (Index), 0661d7a0..dc: Frames (+0x10) + Stride (+0x14) *
static const unsigned char* ChannelFramePayload(const FChannelArchetypeObject* Channel,
                                                unsigned long Index)
{
    const unsigned long Skip = (double)(Channel->SampleRate) < 0.0 ? 4 : 0;
    return Channel->Frames + Channel->Stride * Index + Skip;
}

// The frame-time idiom every ChannelArchetype sampler inlines (0661d872..a5 in FUN_0661d820,
static double ChannelFrameTime(const FChannelArchetypeObject* Channel, unsigned long Index)
{
    if (!((double)(Channel->SampleRate) >= 0.0))
    {
        float Time = 0.0f;
        std::memcpy(&Time, Channel->Frames + Channel->Stride * Index, sizeof(Time));
        return (double)Time;
    }
    return (double)((unsigned long long)Index) *
           (double)(Channel->SampleRate);
}

// FUN_0661d820, thiscall RET 0x4 (Time), 0661d820..c8. No checks.
static unsigned long ChannelFrameIndexAt(const FChannelArchetypeObject* Channel, float Time)
{
    if ((double)(Channel->SampleRate) > 0.0)
    {
        const unsigned long Last = Channel->FrameCount - 1;
        const long long Truncated = (long long)((double)Time /
                                                           (double)(Channel->SampleRate));
        const unsigned long Index = (unsigned long)Truncated + 1;
        return Last < Index ? Last : Index;
    }
    unsigned long Lo = 0;
    unsigned long Hi = Channel->FrameCount - 1;
    unsigned long Mid = Hi >> 1;
    if (Hi == 0)
        return Mid;
    do
    {
        if (!(ChannelFrameTime(Channel, Mid) <= (double)Time))
            Hi = Mid;
        else
            Lo = Mid + 1;
        Mid = (Hi + Lo) >> 1;
    } while (Lo < Hi);
    return Mid;
}

// One output dword of FUN_0661db70 (`MOV [ESI],reg / ADD ESI,4` or `FSTP float [ESI]`).
static void ChannelPutDword(unsigned char*& Dst, const void* Value)
{
    std::memcpy(Dst, Value, 4);
    Dst += 4;
}

static void ChannelPutFloat(unsigned char*& Dst, float Value)
{
    ChannelPutDword(Dst, &Value);
}

// A packed quaternion component: MOVSX word, FILD, FMUL float DAT_0662955c (1/32767). A 16-bit
// integer times a float is exact in a double, so this is the value the FPU holds.
static double ChannelUnpackShort(const unsigned char* Src)
{
    short Packed = 0;
    std::memcpy(&Packed, Src, sizeof(Packed));
    return (double)Packed * (double)G_ShortToUnit;
}

// FUN_0661db70, thiscall, plain RET (called only from ChannelArchetype::Load at 0661dad6).
static void ChannelDecompressFrames(FChannelArchetypeObject* Channel)
{
    const unsigned long Flags = Channel->Flags;
    if ((Flags & 0xF0) == 0 && (Flags & 0x100) == 0)
        return;
    if ((Flags & 9) != 0)
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x13C, "FATAL",
                     "Cannot uncompress data other than vectors and quaternions");
        return;
    }
    const bool bStamp = (double)(Channel->SampleRate) < 0.0;
    const bool bPos = (Flags & G_ChannelFlagPosition) != 0;
    const bool bZeroPos = (Flags & G_ChannelFlagZeroPos) != 0;
    const bool bHasPos = bPos || bZeroPos;
    if (bPos && bZeroPos)
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x15F, "FATAL",
                     "Inconsistent specification for vector data");
        return;
    }
    const bool bQuat = (Flags & G_ChannelFlagQuat) != 0;
    const bool bIdent = (Flags & G_ChannelFlagIdentQuat) != 0;
    const bool bComp3 = (Flags & G_ChannelFlagCompQuat3) != 0;
    const bool bAxis = (Flags & G_ChannelFlagCompAxis) != 0;
    const bool bComp4 = (Flags & G_ChannelFlagCompQuat4) != 0;
    const bool bHasQuat = bQuat || bIdent || bComp3 || bAxis || bComp4;
    const bool bAnyPacked = bComp3 || bAxis || bComp4;
    if ((bQuat && bIdent) || ((bQuat || bIdent) && bAnyPacked) || (bComp3 && bAxis) ||
        (bComp3 && bComp4) || (bAxis && bComp4))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x199, "FATAL",
                     "Inconsistent specification for quaternion data");
        return;
    }

    unsigned long Words = bStamp ? 1 : 0;
    if (bHasPos)
        Words += 3;
    if (bHasQuat)
        Words += 4;
    unsigned char* const Out =
        (unsigned char*)(std::malloc(Channel->FrameCount * Words * 4));
    Channel->Flags = Channel->Flags & 0xFFFFFE0F;
    if (bHasPos)
        Channel->Flags = Channel->Flags | G_ChannelFlagPosition;
    if (bHasQuat)
        Channel->Flags = Channel->Flags | G_ChannelFlagQuat;
    Channel->Stride = ChannelStrideFromHeader(Channel->Flags, Channel->SampleRate);

    const unsigned char* Src = Channel->Frames;
    unsigned char* Dst = Out;
    for (unsigned long Frame = 0; Frame < Channel->FrameCount; ++Frame)
    {
        if (bStamp)
        {
            ChannelPutDword(Dst, Src);
            Src += 4;
        }
        if (bHasPos)
        {
            if (bPos)
            {
                ChannelPutDword(Dst, Src);
                ChannelPutDword(Dst, Src + 4);
                ChannelPutDword(Dst, Src + 8);
                Src += 0xC;
            }
            else if (bZeroPos)
            {
                ChannelPutFloat(Dst, 0.0f);
                ChannelPutFloat(Dst, 0.0f);
                ChannelPutFloat(Dst, 0.0f);
            }
            else
            {
                EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x1F0, "FATAL",
                             "Cannot generate vector data");
            }
        }
        if (!bHasQuat)
            continue;
        if (bQuat)
        {
            ChannelPutDword(Dst, Src);
            ChannelPutDword(Dst, Src + 4);
            ChannelPutDword(Dst, Src + 8);
            ChannelPutDword(Dst, Src + 0xC);
            Src += 0x10;
        }
        else if (bIdent)
        {
            ChannelPutFloat(Dst, 1.0f);
            ChannelPutFloat(Dst, 0.0f);
            ChannelPutFloat(Dst, 0.0f);
            ChannelPutFloat(Dst, 0.0f);
        }
        else if (bComp3)
        {
            // 0661def2..df8c: X and Y stay on the FPU, Z goes through float [ESP+0x1c].
            const double X = ChannelUnpackShort(Src);
            const double Y = ChannelUnpackShort(Src + 2);
            const float Z = (float)(ChannelUnpackShort(Src + 4));
            Src += 6;
            const double Sum = (double)Z * (double)Z + Y * Y + X * X;
            const float SumStored = (float)Sum;
            double W = 0.0;
            if (Sum < 1.0)
                W = std::sqrt(1.0 - (double)SumStored);
            ChannelPutFloat(Dst, (float)W);
            ChannelPutFloat(Dst, (float)X);
            ChannelPutFloat(Dst, (float)Y);
            ChannelPutFloat(Dst, Z);
        }
        else if (bAxis)
        {
            // 0661df9d..e056: Z is stored as float and the FPU value multiplied by it
            const double X = ChannelUnpackShort(Src);
            const double Y = ChannelUnpackShort(Src + 2);
            const double ZFull = ChannelUnpackShort(Src + 4);
            const float Z = (float)ZFull;
            Src += 6;
            const double Sum = ZFull * (double)Z + Y * Y + X * X;
            if (Sum > 0.0)
            {
                const double Length = std::sqrt(Sum);
                const double Sine = std::sin((double)(3.1415927f) * Length * 0.5);
                const float SineStored = (float)Sine;
                const float Scale = (float)(Sine / Length);
                const double W = std::sqrt(1.0 - (double)SineStored *
                                                     (double)SineStored);
                ChannelPutFloat(Dst, (float)W);
                ChannelPutFloat(Dst, (float)(X * (double)Scale));
                ChannelPutFloat(Dst, (float)(Y * (double)Scale));
                ChannelPutFloat(Dst, (float)((double)Scale *
                                                        (double)Z));
            }
            else
            {
                ChannelPutFloat(Dst, 1.0f);
                ChannelPutFloat(Dst, (float)X);
                ChannelPutFloat(Dst, (float)Y);
                ChannelPutFloat(Dst, Z);
            }
        }
        else if (bComp4)
        {
            // 0661e067..e12c: X and Y stay on the FPU, Z goes through float, W is stored as
            // float and the FPU value multiplied by it. Sum = W*Wf + Zf*Zf + Y*Y + X*X,
            // M = (3.0f - Sum) * 0.5f, stored as Mf. Out = {M * X, Mf * Y, Mf * Zf, Mf * Wf}.
            const double X = ChannelUnpackShort(Src);
            const double Y = ChannelUnpackShort(Src + 2);
            const float Z = (float)(ChannelUnpackShort(Src + 4));
            const double WFull = ChannelUnpackShort(Src + 6);
            const float W = (float)WFull;
            Src += 8;
            const double Sum = WFull * (double)W +
                               (double)Z * (double)Z + Y * Y + X * X;
            const double Mid = (3.0 - Sum) * 0.5;
            const float MidStored = (float)Mid;
            ChannelPutFloat(Dst, (float)(Mid * X));
            ChannelPutFloat(Dst, (float)((double)MidStored * Y));
            ChannelPutFloat(Dst, (float)((double)MidStored *
                                                    (double)Z));
            ChannelPutFloat(Dst, (float)((double)MidStored *
                                                    (double)W));
        }
        else
        {
            EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x28E, "FATAL",
                         "Cannot generate quaternion data");
        }
    }
    Channel->Frames = Out;
}

// LAB_0661d8d0, IChannelArchetype slot 3 (Load), RET 0x8. This is not tested.
static GENRESULT DACOM_API ChannelArchetypeLoad(FChannelArchetypeObject* This, void* FileSystem)
{
    if (FileSystem == nullptr)
        return GR_INVALID_PARMS;
    IFileSystem* Fs = AsFileSystem(FileSystem);

    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, G_NameHeader, G_NameDos);
    const int HeaderHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (HeaderHandle == -1)
        return (GENRESULT)0xFFFFFFFA;
    unsigned long Header[3];
    unsigned long BytesRead = 0;
    const int HeaderOk = Fs->Read((unsigned long)HeaderHandle, Header, 0xC,
                                  &BytesRead, 0); // +0x14
    Fs->CloseFile((unsigned long)HeaderHandle); // +0x10
    if (HeaderOk == 0 || BytesRead != 0xC)
        return (GENRESULT)0xFFFFFFFA;

    This->FrameCount = Header[0];
    std::memcpy(&This->SampleRate, &Header[1], sizeof(This->SampleRate));
    This->Flags = Header[2];
    This->Stride = ChannelStrideFromHeader(This->Flags, This->SampleRate);

    Desc.DataPath = G_NameFrames;
    const int FramesHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (FramesHandle == -1)
        return (GENRESULT)0xFFFFFFFA;
    const unsigned long FileSize = Fs->GetSize((unsigned long)FramesHandle, 0); // +0x28
    if (This->Flags != G_ChannelFlagScalarOnly && FileSize != This->FrameCount * This->Stride)
    {
        Fs->CloseFile((unsigned long)FramesHandle); // +0x10
        return GR_GENERIC;
    }
    if ((This->Flags & 0xF0) != 0 || (This->Flags & 0x100) != 0)
    {
        if (FileSize > G_ChannelFramePoolSize)
        {
            G_ChannelFramePool = (unsigned char*)(std::realloc(G_ChannelFramePool, FileSize));
            G_ChannelFramePoolSize = FileSize;
        }
        This->Frames = G_ChannelFramePool;
    }
    else
    {
        This->Frames = (unsigned char*)(std::malloc(FileSize));
    }
    if (This->Frames == nullptr)
        return GR_OUT_OF_MEMORY;
    const int FramesOk = Fs->Read((unsigned long)FramesHandle, This->Frames, FileSize,
                                  &BytesRead, 0); // +0x14
    Fs->CloseFile((unsigned long)FramesHandle); // +0x10
    if (FramesOk == 0 || BytesRead != FileSize)
        return (GENRESULT)0xFFFFFFFA;

    ChannelDecompressFrames(This);
    const unsigned long Last = This->FrameCount - 1;
    if ((double)(This->SampleRate) < 0.0)
    {
        float Time = 0.0f;
        std::memcpy(&Time, This->Frames + This->Stride * Last, sizeof(Time));
        This->Duration = Time;
    }
    else
    {
        This->Duration = (float)((double)((unsigned long long)Last) *
                                            (double)(This->SampleRate));
    }
    return GR_OK;
}

// LAB_0661d800, IChannelArchetype slot 4, RET 0x8: *Out = Duration (+0x18), EAX = 0. No
// checks (the port used to test Out and This).
static GENRESULT DACOM_API ChannelArchetypeGetDuration(FChannelArchetypeObject* This, float* Out)
{
    *Out = This->Duration;
    return GR_OK;
}

// LAB_0661d7e0, IChannelArchetype slot 5, RET 0x8: *Out = Flags (+0xC), EAX = 0. No checks
// (the port used to test Out and This).
static GENRESULT DACOM_API ChannelArchetypeGetFlags(FChannelArchetypeObject* This, unsigned long* Out)
{
    *Out = This->Flags;
    return GR_OK;
}

// LAB_0661e1a0, IChannelArchetype slot 6, RET 0x14 (Time, OutA, OutB, Alpha), 0661e1a0..2d4.
static GENRESULT DACOM_API ChannelArchetypeSample(FChannelArchetypeObject* This, float Time,
                                                 const unsigned char** OutA,
                                                 const unsigned char** OutB, float* Alpha)
{
    float Duration = 0.0f;
    This->GetDuration(&Duration);
    if (!((double)Time >= 0.0))
        return (GENRESULT)0xFFFFFFFE;
    if (!(Time <= Duration))
        return (GENRESULT)0xFFFFFFFE;
    const unsigned long Hi = ChannelFrameIndexAt(This, Time);
    const double TimeHi = ChannelFrameTime(This, Hi);
    if (!(TimeHi > (double)Time))
    {
        const unsigned char* Payload = ChannelFramePayload(This, Hi);
        *OutB = Payload;
        *OutA = Payload;
        *Alpha = 0.0f;
        return GR_OK;
    }
    const unsigned long Lo = Hi - 1;
    const double TimeLo = ChannelFrameTime(This, Lo);
    *Alpha = (float)(((double)Time - TimeLo) / (TimeHi - TimeLo));
    *OutA = ChannelFramePayload(This, Lo);
    *OutB = ChannelFramePayload(This, Hi);
    return GR_OK;
}

// LAB_0661e670, IChannelArchetype slot 7, RET 0x1C (Time, OutLo, OutHi, TimeLo, TimeHi,
static GENRESULT DACOM_API ChannelArchetypeSamplePair(FChannelArchetypeObject* This, float Time,
                                                     const unsigned char** OutLo,
                                                     const unsigned char** OutHi, float* TimeLo,
                                                     float* TimeHi, float* Alpha)
{
    float Duration = 0.0f;
    This->GetDuration(&Duration);
    if (!((double)Time >= 0.0))
        return (GENRESULT)0xFFFFFFFE;
    if (!(Time <= Duration))
        return (GENRESULT)0xFFFFFFFE;
    const unsigned long Hi = ChannelFrameIndexAt(This, Time);
    const double FrameTime = ChannelFrameTime(This, Hi);
    if (!(FrameTime > (double)Time))
    {
        const unsigned char* Payload = ChannelFramePayload(This, Hi);
        *OutHi = Payload;
        *OutLo = Payload;
        const double Again = ChannelFrameTime(This, Hi);
        *TimeHi = (float)Again;
        *TimeLo = (float)Again;
        *Alpha = 0.0f;
        return GR_OK;
    }
    const unsigned long Lo = Hi - 1;
    *TimeLo = (float)(ChannelFrameTime(This, Lo));
    *TimeHi = (float)FrameTime;
    *Alpha = (float)(((double)Time - (double)(*TimeLo)) /
                                (FrameTime - (double)(*TimeLo)));
    *OutLo = ChannelFramePayload(This, Lo);
    *OutHi = ChannelFramePayload(This, Hi);
    return GR_OK;
}

// LAB_0661e2e0, IChannelArchetype slot 8, RET 0xC (bLast, Out), 0661e2e0..308: index 0, or
static GENRESULT DACOM_API ChannelArchetypeGetFrame(FChannelArchetypeObject* This,
                                                   unsigned char bLast, const unsigned char** Out)
{
    long Index = 0;
    if (bLast != 0)
    {
        Index = (long)(This->FrameCount - 1);
        if (Index < 0)
            return (GENRESULT)0xFFFFFFFF;
    }
    *Out = ChannelFramePayload(This, (unsigned long)Index);
    return GR_OK;
}

// .rdata:06629560 / 06629564 (PE bytes 95 BF D6 B3 / 95 BF D6 33): -1e-7f and +1e-7f.
static const float G_ChannelTimeEpsNeg = -1.0000000116860974e-07f;
static const float G_ChannelTimeEpsPos = 1.0000000116860974e-07f;

// FUN_0661e3d0, thiscall RET 0x4 (Index), 0661e3d0..ee: *(dword*)FUN_0661d7a0(Index) +
// Frames (+0x10) + Stride (+0x14) * FrameCount (+4). No checks (the port used to return 0
// for a null payload, channel or Frames).
static unsigned long ChannelRelocatedKey(const FChannelArchetypeObject* Channel, unsigned long Index)
{
    const unsigned char* Payload = ChannelFramePayload(Channel, Index);
    unsigned long Reloc = 0;
    std::memcpy(&Reloc, Payload, sizeof(Reloc));
    return Channel->Stride * Channel->FrameCount + (unsigned long)(Channel->Frames) +
           Reloc;
}

// LAB_0661e400, IChannelArchetype slot 9, RET 0x18 (Time0, Time1, OutKeys, Capacity, Count),
static GENRESULT DACOM_API ChannelArchetypeCollectRange(FChannelArchetypeObject* This, float Time0,
                                                       float Time1, float* OutKeys,
                                                       unsigned long Capacity, unsigned long* Count)
{
    *Count = 0;
    if (This->Flags != G_ChannelFlagScalarOnly)
        return (GENRESULT)0xFFFFFFFF;
    float Duration = 0.0f;
    This->GetDuration(&Duration);
    const float DurEps = Duration + G_ChannelTimeEpsPos;
    if (!(Time0 >= G_ChannelTimeEpsNeg) || !(Time0 <= DurEps))
        return (GENRESULT)0xFFFFFFFE;
    if (!(Time1 >= G_ChannelTimeEpsNeg) || !(Time1 <= DurEps))
        return (GENRESULT)0xFFFFFFFE;
    if (Time0 == Time1)
        return (GENRESULT)0xFFFFFFFE;

    long Start = 0;
    std::memcpy(&Start, &Time0, sizeof(Start));
    long Stop = 0;
    std::memcpy(&Stop, &DurEps, sizeof(Stop));
    long Step = 1;
    if (Time0 < Time1)
    {
        for (long Index = 0; Index < (long)(This->FrameCount); ++Index)
        {
            const double FrameTime = ChannelFrameTime(This, (unsigned long)Index);
            if (FrameTime <= (double)Time0)
                Start = Index;
            if (!(FrameTime < (double)Time1))
                break;
            Stop = Index;
        }
        Start += 1;
        Stop += 1;
    }
    else
    {
        Step = -1;
        for (long Index = (long)(This->FrameCount) - 1; Index >= 0; --Index)
        {
            const double FrameTime = ChannelFrameTime(This, (unsigned long)Index);
            if (FrameTime >= (double)Time0)
                Start = Index;
            if (!(FrameTime > (double)Time1))
                break;
            Stop = Index;
        }
        Start -= 1;
        Stop -= 1;
    }

    while (Start != Stop)
    {
        if (*Count < Capacity)
        {
            OutKeys[*Count * 2] =
                (float)(ChannelFrameTime(This, (unsigned long)Start));
            const unsigned long Key = ChannelRelocatedKey(This, (unsigned long)Start);
            std::memcpy(OutKeys + *Count * 2 + 1, &Key, sizeof(Key));
        }
        Start += Step;
        *Count += 1;
    }
    return GR_OK;
}

// LAB_0661e310, IChannelArchetype slot 10, RET 0x14 (Time, OutKeys, Capacity, Count),
static GENRESULT DACOM_API ChannelArchetypeCollectAt(FChannelArchetypeObject* This, float Time,
                                                    float* OutKeys, unsigned long Capacity,
                                                    unsigned long* Count)
{
    *Count = 0;
    if (This->Flags != G_ChannelFlagScalarOnly)
        return (GENRESULT)0xFFFFFFFF;
    GENRESULT Result = GR_OK;
    for (unsigned long Index = 0; Index < This->FrameCount; ++Index)
    {
        if (ChannelFrameTime(This, Index) == (double)Time)
        {
            if (*Count < Capacity)
            {
                OutKeys[*Count * 2] = Time;
                const unsigned long Key = ChannelRelocatedKey(This, Index);
                std::memcpy(OutKeys + *Count * 2 + 1, &Key, sizeof(Key));
            }
            else
                Result = (GENRESULT)0xFFFFFFFF;
            *Count += 1;
        }
    }
    return Result;
}


// FUN_06611190 cdecl (From, Name), 06611190..0661127d. IEngine slot 26 GetInstanceName(Out,
static FEngineInstance* AnimEngineFindInstanceByName(FEngineInstance* From, const char* Name)
{
    FEngineInstance* const Invalid =
        (FEngineInstance*)((ptrdiff_t)(-1));
    FArchetypeName Out;
    const char* Text = G_AnimEngine->GetInstanceName(&Out, From)->Text;
    if (Text == nullptr)
        Text = "";
    if (std::strcmp(Text, Name) == 0)
        return From;
    FEngineInstance* Cursor =
        (FEngineInstance*)(G_AnimEngine->GetRelatedInstance(From, 0, Invalid));
    while (Cursor != Invalid)
    {
        Text = G_AnimEngine->GetInstanceName(&Out, Cursor)->Text;
        if (Text == nullptr)
            Text = "";
        if (std::strcmp(Text, Name) == 0)
            return Cursor;
        Cursor = (FEngineInstance*)(G_AnimEngine->GetRelatedInstance(From, 0, Cursor));
    }
    return Invalid;
}

// FUN_06612320 cdecl (Out, Root, Track, ArgD, ArgE, Already), 06612320..d3. The caller seeds
static unsigned char BindTrackTarget(FAnimBindOut* Out, FEngineInstance* Root, FAnimTrack* Track,
                                    void* ArgD, void* ArgE, unsigned char* Already)
{
    FEngineInstance* const Invalid =
        (FEngineInstance*)((ptrdiff_t)(-1));
    *Already = 0;
    if (Root == Invalid && ArgD == nullptr)
    {
        Out->Kind = 0;
        Out->Extra = nullptr;
        Out->Typed = nullptr;
        Out->Instance = nullptr;
        return 0;
    }
    Out->Typed = ArgE;
    if (Track->ChildCrc != 0)
    {
        const char* ChildName = Track->ChildName != nullptr ? Track->ChildName : "";
        FEngineInstance* Inst = AnimEngineFindInstanceByName(Root, ChildName);
        if (Inst == Invalid)
            return 0;
        const bool bMapped = FindAnimHandle((unsigned long)((size_t)Inst));
        *Already = bMapped ? 1 : 0;
        if (bMapped)
            return 0;
        FEngineInstance* Parent = (FEngineInstance*)(G_AnimEngine->GetInstanceParent(Inst));
        if (Parent == Invalid)
            return 0;
        FArchetypeName NameOut;
        if (Track->ParentCrc != G_AnimEngine->GetInstanceName(&NameOut, Parent)->Crc)
            return 0;
        Out->Kind = 0;
        Out->Instance = Inst;
        Out->Extra = ArgD;
        return 1;
    }
    if (Track->ParentCrc != 0)
    {
        const char* ParentName = Track->ParentName != nullptr ? Track->ParentName : "";
        FEngineInstance* Inst = AnimEngineFindInstanceByName(Root, ParentName);
        if (Inst == Invalid)
            Inst = Root;
        const bool bMapped = FindAnimHandle((unsigned long)((size_t)Inst));
        *Already = bMapped ? 1 : 0;
        if (bMapped)
            return 0;
        Out->Instance = Inst;
        Out->Kind = 1;
        Out->Extra = ArgD;
        return 1;
    }
    Out->Instance = Root;
    Out->Kind = 2;
    Out->Extra = ArgD;
    return ArgD != nullptr ? 1 : 0;
}

// FUN_066124e0 thiscall RET 0xC (Root, ArgD, ArgE), 066124e0..d6. record +4 = Root
static void BindPlaybackTracks(FAnimPlayback* Playback, FEngineInstance* Root, void* ArgD,
                               void* ArgE)
{
    Playback->Root = Root;
    Playback->LongestTyped = nullptr;
    float BestDuration = -1.0f;
    if (Playback->Typed.empty())
        return;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    // The track cursor walks the name value's vector (+0x14 first) alongside, advancing once
    // per iteration whether or not the channel was kept.
    FAnimTrack* Track = Playback->Name->Tracks.data();
    while (Cursor != Playback->Typed.end())
    {
        FAnimBindOut Bind;
        Bind.Kind = 2;
        Bind.Extra = nullptr;
        Bind.Typed = nullptr;
        Bind.Instance = (FEngineInstance*)((ptrdiff_t)-1);
        unsigned char bAlready = 0;
        unsigned char bBound = BindTrackTarget(&Bind, Root, Track, ArgD, ArgE, &bAlready);
        if (bBound != 0)
        {
            FTypedChannel* Typed = *Cursor;
            bBound = (unsigned char)(
                Typed->BindMap(
                    (unsigned long*)(&Bind), Root));
        }
        if (bBound != 0)
        {
            float Duration = 0.0f;
            Track->Channel->GetDuration(&Duration);
            if (BestDuration < Duration)
            {
                Playback->LongestTyped = *Cursor;
                BestDuration = Duration;
            }
            ++Cursor;
        }
        else
        {
            if (bAlready == 0)
            {
                if (Track->ChildCrc != 0)
                {
                    const char* Parent = Track->ParentName != nullptr ? Track->ParentName : "";
                    const char* Child = Track->ChildName != nullptr ? Track->ChildName : "";
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message),
                              "%s failed to map JOINT (parent: %s, child: %s) channel",
                              Playback->Name->Name, Parent, Child);
                    Message[sizeof(Message) - 1] = '\0';
                    EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x2F5, "WARNING", Message);
                }
                else if (Track->ParentCrc != 0)
                {
                    const char* Parent = Track->ParentName != nullptr ? Track->ParentName : "";
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message), "%s failed to map OBJECT (%s) channel",
                              Playback->Name->Name, Parent);
                    Message[sizeof(Message) - 1] = '\0';
                    EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x2FA, "WARNING", Message);
                }
            }
            FTypedChannel* Typed = *Cursor;
            if (Typed != nullptr)
            {
                if (Typed->Archetype != nullptr)
                {
                    Typed->Archetype->Release();
                    Typed->Archetype = nullptr;
                }
                ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.
            }
            Cursor = Playback->Typed.erase(Cursor);
        }
        ++Track;
    }
}

// FUN_066128f0 thiscall RET 0: every channel stops playing (PlayState &= 0xFE) and is posed
// where it stands (FUN_0661b070).
static void PlaybackStopTyped(FAnimPlayback* Playback)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        FTypedChannel* Typed = Playback->Typed[Index];
        Typed->PlayState &= 0xFE;                                // 066128fc..04
        TypedApplyFromArchetype(Typed);                          // 06612909
        Index += 1;
    }
}

// FUN_06612920 thiscall RET 0: FUN_0661b070 on every channel, nothing else.
static void PlaybackApplyTyped(FAnimPlayback* Playback)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        TypedApplyFromArchetype(Playback->Typed[Index]);    // 0661292e
        Index += 1;
    }
}

// Rebind every channel with one play header (FUN_0661ad90 each; 066127af..d0 / 066128b7..d8).
static unsigned long PlaybackBindAll(FAnimPlayback* Playback, float* Header, unsigned long Result)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        if ((TypedTryBindIfTimeValid(Playback->Typed[Index], Header) & 0xFF) == 0)
            Result = 0xFFFFFFFF;                                 // OR EBP,-1 -- no break
        Index += 1;
    }
    return Result;
}

// Reverse playback requires compatible track durations.
static unsigned long PlaybackDurationsDiffer(FAnimPlayback* Playback)
{
    unsigned long Result = 0;
    float First = -1.0f;                                         // 0661274f / 06612857
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        FTypedChannel* Typed = Playback->Typed[Index];
        if (First < 0.0f)
            Typed->Archetype->GetDuration(&First);
        else
        {
            float Duration = 0.0f;
            Typed->Archetype->GetDuration(&Duration);
            if (!(First == Duration))
                Result = 1;
        }
        Index += 1;
    }
    return Result;
}

// FUN_066126e0 thiscall RET 0x1C (Flags, Start, Speed, P3, P4, P5, P6): build the seven-dword
static unsigned long PlaybackEvalTyped(FAnimPlayback* Playback, unsigned long Flags, float Start,
                                      float Speed, unsigned long P3, unsigned long P4,
                                      unsigned long P5, unsigned long P6)
{
    // SETZ DL on (Flags & 1); LEA EDX,[EDX*2-1]: +1 when the bit is clear, -1 when set.
    // FLD Speed; FABS; FILD sign; FMULP; FSTP dword.
    const long Sign = (Flags & 1) == 0 ? 1 : -1;
    const float Rate = (float)(std::fabs((double)Speed) *
                                          (double)Sign);
    unsigned long Header[7];
    std::memcpy(&Header[0], &Start, sizeof(float));
    Header[1] = Flags & 0xFE;
    std::memcpy(&Header[2], &Rate, sizeof(float));
    Header[3] = P3;
    Header[4] = P4;
    Header[5] = P5;
    Header[6] = P6;
    unsigned long Result = 0;
    if ((Flags & 1) != 0 && Playback->Typed.size() != 0)      // 0661273d..57
        Result = PlaybackDurationsDiffer(Playback);
    return PlaybackBindAll(Playback, (float*)Header, Result);
}

// FUN_066127e0 thiscall RET 0: rebind every channel from the longest one's own state:
static unsigned long PlaybackEvalStoredTyped(FAnimPlayback* Playback)
{
    FTypedChannel* Longest = Playback->LongestTyped;
    const float Start = -2.0f;
    unsigned long Header[7];
    std::memcpy(&Header[0], &Start, sizeof(float));
    Header[1] = Longest->LoopFlags & 0xFE;
    std::memcpy(&Header[2], &Longest->Weight, sizeof(float));
    Header[3] = 0;
    std::memcpy(&Header[4], &Longest->Scale, sizeof(float));
    std::memcpy(&Header[5], &Longest->Field_0x30, sizeof(float));
    Header[6] = 0;
    return PlaybackBindAll(Playback, (float*)Header, 0);
}

// FUN_06612940 thiscall RET 4: the channel whose bind descriptor (typed +8) matches Header:
// same kind, then kinds 0/1 compare the instance (+0xC), kind 2 the sink (+4); other kinds
// never match. Miss -> -1.
static FTypedChannel* PlaybackFindTypedByHeader(FAnimPlayback* Playback, const FAnimBindOut* Header)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        FTypedChannel* Typed = Playback->Typed[Index];
        const FAnimBindOut* Desc = TypedBindDesc(Typed);
        if (Desc->Kind == Header->Kind)                          // 06612966..7c
        {
            if (Desc->Kind == 0 || Desc->Kind == 1)              // 0661297e..84 -> 0661298e
            {
                if (Desc->Instance == Header->Instance)
                    return Typed;
            }
            else if (Desc->Kind == 2)                            // 06612986..89
            {
                if (Desc->Extra == Header->Extra)
                    return Typed;
            }
        }
        Index += 1;
    }
    return (FTypedChannel*)((ptrdiff_t)(-1));
}

// FUN_066129c0 thiscall RET 4: typed slot 0x10 (SetTime) on every channel.
static void PlaybackSetTypedTime(FAnimPlayback* Playback, float Time)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        FTypedChannel* Typed = Playback->Typed[Index];
        Typed->SetTime(Time);
        Index += 1;
    }
}

// FUN_066129f0 thiscall RET 0: MOV EAX,[this+0x18]; FLD [EAX+0x1C] -- the longest channel's
static float PlaybackStoredDuration(FAnimPlayback* Playback)
{
    return Playback->LongestTyped->Time;
}

// FUN_06611290, thiscall on the track (unused), RET 0xC (FileSystem, Name, Out). No checks.
static unsigned long ReadUtfNameCrc(void* FileSystem, const char* Name, char** Out)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle != -1)
    {
        const unsigned long Size = Fs->GetSize((unsigned long)Handle, 0); // +0x28
        *Out = (char*)(std::malloc(Size));
        unsigned long BytesRead = 0;
        Fs->Read((unsigned long)Handle, *Out, Size, &BytesRead, 0); // +0x14
        Fs->CloseFile((unsigned long)Handle); // +0x10
    }
    return DACOM_CRC::GetCRC32(*Out);
}

// FUN_06611350, thiscall on the track, RET 4 (FileSystem). No checks.
static void LoadChannelNode(FAnimTrack* Track, void* FileSystem)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameChannel) == 0) // +0x60
        return;
    if (Track->Channel != nullptr)
    {
        Track->Channel->Release();
        Track->Channel = nullptr;
    }
    FAggDesc8 Desc;
    Desc.Size = G_AggDescSizeChannelArchetype;
    Desc.Name = G_NameChannelArchetype;
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom->CreateInstance((AGGDESC*)(&Desc),
                              (void**)(&Track->Channel)) == 0) // +0xC
        Track->Channel->Load(FileSystem); // +0xC
    Fs->OpenNode(G_NameParentDot); // +0x60
}

// FUN_06611440, thiscall on the name value, RET 8 (Name, FileSystem); returns AL = 1, which no
static void LoadAnimScriptMaps(FAnimNameEntry* Entry, const char* Name, void* FileSystem)
{
    Entry->Name = _strdup(Name);
    IFileSystem* Fs = AsFileSystem(FileSystem);
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar, // +0x48
                                      &FindData);
    if (FindHandle == -1)
        return;
    float BestDuration = -1.0f;
    do
    {
        if ((FindData.dwFileAttributes & 0x10) == 0)
        {
            if (std::strcmp(FindData.cFileName, G_NameRootHeight) != 0)
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.Field30 = (unsigned long)FindHandle;
            const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
            if (Handle == -1)
                continue;
            unsigned long BytesRead = 0;
            Fs->Read((unsigned long)Handle, &Entry->RootHeight, 4, &BytesRead, 0); // +0x14
            Fs->CloseFile((unsigned long)Handle); // +0x10
            Entry->bHasRootHeight = 1;
            continue;
        }
        if (FindData.cFileName[0] == '.')
            continue;
        FEngBaseFileSystemDesc Desc;
        FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
        Desc.Field30 = (unsigned long)FindHandle;
        void* MapFs = nullptr;
        Fs->CreateInstance((AGGDESC*)(&Desc), &MapFs); // +0xC
        if (std::strncmp(FindData.cFileName, G_NameEventMap, G_EventMapLength) != 0)
        {
            // 06611614..dc: resize(size + 1) with a zeroed track; 066116e1..fc: the new last
            // element (+0x18 end - 0x14) is the one filled below.
            Entry->Tracks.resize(Entry->Tracks.size() + 1, FAnimTrack());
            FAnimTrack* Track = &Entry->Tracks.back();
            if (std::strncmp(FindData.cFileName, G_NameJointMap, G_JointMapLength) == 0)
            {
                Track->ParentCrc = ReadUtfNameCrc(MapFs, G_NameParentName, &Track->ParentName);
                Track->ChildCrc = ReadUtfNameCrc(MapFs, G_NameChildName, &Track->ChildName);
                LoadChannelNode(Track, MapFs);
            }
            else if (std::strncmp(FindData.cFileName, G_NameObjectMap, G_ObjectMapLength) == 0)
            {
                Track->ParentCrc = ReadUtfNameCrc(MapFs, G_NameParentName, &Track->ParentName);
                LoadChannelNode(Track, MapFs);
            }
            else if (std::strncmp(FindData.cFileName, G_NameEventMap, G_EventMapLength) == 0)
            {
                LoadChannelNode(Track, MapFs);
            }
            if (Track->Channel != nullptr)
            {
                float Duration;
                Track->Channel->GetDuration(&Duration); // +0x10
                if (BestDuration < Duration)
                {
                    if (Entry->LongestChannel != nullptr)
                    {
                        Entry->LongestChannel->Release();
                        Entry->LongestChannel = nullptr;
                    }
                    Entry->LongestChannel = Track->Channel;
                    Entry->LongestChannel->AddRef();
                    BestDuration = Duration;
                }
            }
        }
        if (MapFs != nullptr)
            ((IDAComponent*)MapFs)->Release();
    } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                        &FindData) != 0);
    Fs->GetFileSystemError(); // +0x78
    Fs->FindClose((unsigned long)FindHandle); // +0x50
}

// LAB_06614920 / IAnimation2+9 RET 0x18 (Script, Root, Name, ArgD, ArgE), 06614920..06614a4b.
static unsigned long DACOM_API AnimationPlayScript(FAnimationObject* This, FAnimScript* Script,
                                                  FEngineInstance* Root, const char* Name,
                                                  void* ArgD, void* ArgE)
{
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return 0xFFFFFFFF;
    FAnimPlayback Record;
    Record.Name = nullptr;                                         // FUN_06611f60
    Record.Root = (FEngineInstance*)((ptrdiff_t)(-1));
    Record.LongestTyped = nullptr;
    Record.Script = nullptr;
    ClonePlaybackTypedChannels(&Record, Entry);
    Record.Script = Script;
    BindPlaybackTracks(&Record, Root, ArgD, ArgE);
    std::vector<FTypedChannel*>::iterator Cursor = Record.Typed.begin();
    while (Cursor != Record.Typed.end())
    {
        This->Channels->insert(*Cursor);
        ++Cursor;
    }
    Script->AddRef(); // vtable +4 (066149cd..d0)
    const unsigned long Id = This->NextPlaybackId;
    This->NextPlaybackId = Id + 1;
    This->Playbacks->insert(FAnimPlaybackMap::value_type((long)Id, Record));
    return Id;
}

// LAB_06614bb0 / IAnimation2+0x2C RET 0x24 (Id, Flags, Time, Scale, A, B, C, D): FUN_066126e0
static unsigned long DACOM_API AnimationEvalPlayback(FAnimationObject* This, unsigned long PlaybackId,
                                                    unsigned long Flags, float Time, float Scale,
                                                    void* A, void* B, void* C, void* D)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)          // 06614bc5..cc
        return 0xFFFFFFFF;
    const unsigned long Result = PlaybackEvalTyped(
        Playback, Flags, Time, Scale, (unsigned long)((size_t)A),
        (unsigned long)((size_t)B),
        (unsigned long)((size_t)C),
        (unsigned long)((size_t)D));
    if (Result == 0xFFFFFFFF)                                    // 06614bfc..03
        return Result;
    const float BlendDuration = FloatFromOpaqueDword(A);
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        MixerBlendAttach(This, Playback->Typed[Index], BlendDuration, Flags);
        Index += 1;
    }
    return Result;
}

// LAB_06614c40 / IAnimation2+0x30 RET 8: FUN_066127e0, or -1 for an unknown id.
static unsigned long DACOM_API AnimationEvalStored(FAnimationObject* This, unsigned long PlaybackId)
{
    (void)This;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    return PlaybackEvalStoredTyped(Playback);
}

// LAB_06614c70 / IAnimation2+0x38 RET 8: FUN_066128f0 (stop and pose every channel), then
// every channel leaves its mixer (FUN_06615ec0).
static unsigned long DACOM_API AnimationStopPlayback(FAnimationObject* This, unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    PlaybackStopTyped(Playback);                                 // 06614c92
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())                    // 06614c97..b3
    {
        MixerUnbindTyped(This, Playback->Typed[Index]);
        Index += 1;
    }
    return 0;
}

// LAB_06614cc0 / IAnimation2+0x34 RET 8: FUN_06612920 -- pose every channel, nothing more.
static unsigned long DACOM_API AnimationApplyChannels(FAnimationObject* This, unsigned long PlaybackId)
{
    (void)This;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) != 0)
        PlaybackApplyTyped(Playback);
    return 0;
}

// LAB_06614860 / IAnimation2+0x3C RET 0xC: FUN_066129c0 -- typed slot 0x10 (SetTime) with the
// third argument on every channel. The interface carries that dword as void*.
static unsigned long DACOM_API AnimationStopChannels(FAnimationObject* This, unsigned long PlaybackId,
                                                    void* Time)
{
    (void)This;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) != 0)
        PlaybackSetTypedTime(Playback, FloatFromOpaqueDword(Time));
    return 0;
}

// LAB_06614de0 / IAnimation2+0x48 RET 0xC: the playback's root entry in the mixer map
// (FUN_06614f70 on playback +4); when there is one, FUN_06612a70 writes the position into the
// channels and the object overlays.
static unsigned long DACOM_API AnimationSetOverlayPos(FAnimationObject* This, unsigned long PlaybackId,
                                                     float* Pos)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        MixerFindKey(This, (long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        PlaybackWriteOverlayPos(Playback, Pos, Inner);
    return 0;
}

// LAB_06614fd0 / IAnimation2+0x4C RET 0xC: as above with FUN_06612b80 (orientation matrix).
static unsigned long DACOM_API AnimationSetOverlayExtra(FAnimationObject* This, unsigned long PlaybackId,
                                                       float* Extra)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        MixerFindKey(This, (long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        PlaybackWriteOverlayOrient(Playback, Extra, Inner);
    return 0;
}

// LAB_066150e0 / IAnimation2+0x54 RET 0x10 (PlaybackId, Pos, Extra): FUN_06614580; a miss
// answers 0 (ESI, 066150ef / 06615111), a hit FUN_066122d0(Pos, Extra) (06615102..08). Until
// 2026-09-30 the port resolved the id through its global list with a script-pointer retry.
static unsigned long DACOM_API AnimationTouchMapped(FAnimationObject* This, unsigned long PlaybackId,
                                                   float* Pos, float* Extra)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    return PlaybackTouchMappedTyped(Playback, Pos, Extra);
}

// LAB_06615120 / IAnimation2+0x58 RET 0xC: as SetOverlayPos with FUN_06612c60 (add a delta).
static unsigned long DACOM_API AnimationAddOverlayPos(FAnimationObject* This, unsigned long PlaybackId,
                                                     float* Delta)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        MixerFindKey(This, (long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        PlaybackAddOverlayPos(Playback, Delta, Inner);
    return 0;
}

// LAB_06616800 / IAnimation2+0x6C RET 0xC (Key, Dt): walk every mixer list under one bind key
// -- a playback's root, or the cookie an IChannel caller bound with. Not a playback id.
static unsigned long DACOM_API AnimationMixerWalk(FAnimationObject* This, unsigned long Key,
                                                 float Time)
{
    MixerWalkKey(This, (long)Key, Time);
    return 0;
}

// LAB_066157f0 / IAnimation2+0x70 RET 8 (Dt): every mixer list, then every playing channel no
// mixer leaf ticked.
static unsigned long DACOM_API AnimationMixerWalkAll(FAnimationObject* This, float Time)
{
    MixerWalkAll(This, Time);
    return 0;
}

// LAB_06616430 / IChannel+0x1C RET 0xC (Key, Header): rebind the channel with the caller's
static unsigned long DACOM_API ChannelPlay(void* ChannelThis, unsigned long Key, float* Time)
{
    if (Key == 0xFFFFFFFF || Time == nullptr)                    // 06616439..49
        return 0;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0;
    const unsigned long Bound = TypedTryBindIfTimeValid(Typed, Time);
    Typed->PlayState = (unsigned char)(
        ((Typed->PlayState ^ (unsigned char)Bound) & 1) ^ Typed->PlayState);
    const unsigned char bPlaying = Typed->PlayState & 1;
    if (bPlaying != 0)
    {
        const unsigned long* Header = (const unsigned long*)Time;
        MixerBlendAttach(Animation, Typed, Time[3], Header[1]);
    }
    return bPlaying;
}

// LAB_06614b30 / IRootData+0x0C RET 0x24 (Id, TouchArg, Flags, Time, Scale, Arg6, Arg7, Arg8):
static unsigned char DACOM_API RootDataEval(void* RootThis, unsigned long PlaybackId,
                                           void* TouchArg, unsigned long Flags, float Time,
                                           float Scale, void* Arg6, void* Arg7, float Arg8)
{
    // 06614b43: LEA ECX,[EDX-0xC] -- the IRootData facet sits at Animation+0xC.
    FAnimationObject* This = AnimationFromNested(RootThis, 0xC);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    unsigned long Arg8Bits = 0;
    std::memcpy(&Arg8Bits, &Arg8, sizeof(Arg8Bits));
    const unsigned long Eval = PlaybackEvalTyped(
        Playback, Flags, Time, Scale, (unsigned long)((size_t)Arg6),
        (unsigned long)((size_t)Arg7), Arg8Bits, 0);
    if (Eval == 0xFFFFFFFF)
        return 0;
    // 0x06614b85: the first argument is the address of a caller-frame local, the second is
    // this function's own second argument.
    unsigned char TouchLocal[0xC];                               // the SUB ESP,0xC frame
    std::memset(TouchLocal, 0, sizeof(TouchLocal));
    unsigned char bTouch = 0;
    if (PlaybackTouchMappedTyped(Playback, TouchLocal, TouchArg) != 0)
        bTouch = 1;
    PlaybackStopTyped(Playback);                                 // 06614b98..9a FUN_066128f0
    return bTouch;
}


static FArchetypeHandle* DACOM_API EngineCreateArchetypeHandle(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem);


// The "Object name" / "File name" reads of FUN_066201b0 (06620249..c7 / 066202c7..5c): open
static bool ReadPartNameField(IFileSystem* Fs, const char* Field, char* Buffer)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Field, nullptr);
    const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle == -1)
        return false;
    unsigned long Size = Fs->GetSize((unsigned long)Handle, 0); // +0x28
    if (!(Size < 0x3F))
        Size = 0x3F;
    bool bRead = false;
    unsigned long BytesRead = 0;
    if (Fs->Read((unsigned long)Handle, Buffer, Size, &BytesRead, 0) != 0 && // +0x14
        BytesRead == Size)
    {
        bRead = true;
        Buffer[BytesRead] = '\0';
    }
    Fs->CloseFile((unsigned long)Handle); // +0x10
    return bRead;
}

// FUN_066201b0, thiscall on the engine, RET 0x14 (Name, CmpndFs, FindHandle, ParentFs, Child).
static GENRESULT LinkCmpndPartHandle(FEngineObject* Engine, const char* Name, void* CmpndFs,
                                    unsigned long FindHandle, void* ParentFs, FArchetypeChild* Child)
{
    FEngBaseFileSystemDesc PartDesc;
    FillFileSystemDesc(&PartDesc, Name, nullptr);
    PartDesc.Field30 = FindHandle;
    void* FileFs = nullptr;
    void* PartFs = nullptr;
    GENRESULT ObjectResult = (GENRESULT)0xFFFFFFFF;
    GENRESULT FileResult = (GENRESULT)0xFFFFFFFF;
    char FileName[0x40];
    if (AsFileSystem(CmpndFs)->CreateInstance((AGGDESC*)(&PartDesc), &PartFs) == 0)
    {
        IFileSystem* Fs = AsFileSystem(PartFs);
        ObjectResult = ReadPartNameField(Fs, G_NameObjectName, Child->Name) ? GR_OK : GR_E_FAIL;
        FileResult = ReadPartNameField(Fs, G_NameFileName, FileName) ? GR_OK : GR_E_FAIL;
    }
    if (PartFs != nullptr)
    {
        ((IDAComponent*)PartFs)->Release();
        PartFs = nullptr;
    }
    if (ObjectResult < 0 || FileResult < 0)
        return GR_E_FAIL;

    if (Engine->CreateFileSystem(FileName, &FileFs, ParentFs) != 0) // +0x28
    {
        if (FileFs != nullptr)
        {
            ((IDAComponent*)FileFs)->Release();
            FileFs = nullptr;
        }
        if (Engine->CreateFileSystem(FileName, &FileFs, nullptr) != 0) // +0x28
        {
            if (FileFs != nullptr)
                ((IDAComponent*)FileFs)->Release();
            return GR_E_FAIL;
        }
    }

    FArchetypeName Nested;
    Nested.Text = FileName;
    Nested.Crc = DACOM_CRC::GetCRC32(FileName);
    void* PartHandle = Engine->CreateArchetypeHandle(&Nested, FileFs); // +0x30
    if (PartHandle == G_InvalidArchetype)
    {
        if (FileFs != nullptr)
            ((IDAComponent*)FileFs)->Release();
        return GR_E_FAIL;
    }
    Child->Handle = (FArchetypeHandle*)PartHandle;
    Child->Crc = DACOM_CRC::GetCRC32(Child->Name);
    if (FileFs != nullptr)
        ((IDAComponent*)FileFs)->Release();
    return GR_OK;
}

static const unsigned long G_JointTypeFix = 0;
static const unsigned long G_JointTypePris = 1;
static const unsigned long G_JointTypeRev = 2;
static const unsigned long G_JointTypeCyl = 3;
static const unsigned long G_JointTypeSphere = 4;
static const unsigned long G_JointTypeTrans = 5;
static const unsigned long G_JointTypeLoose = 6;
static const unsigned long G_JointUtfNameBytes = 0x40;
static const unsigned long G_JointUtfHeaderBytes = 0x80;
static const unsigned long G_JointUtfMaxBytes = 0xD8;

static unsigned long JointTypeFromName(const char* Name)
{
    if (Name == nullptr)
        return 0xFFFFFFFF;
    if (DACOM_CRC::CompareStringsI(Name, G_NameFix) == 0)
        return G_JointTypeFix;
    if (DACOM_CRC::CompareStringsI(Name, G_NamePris) == 0)
        return G_JointTypePris;
    if (DACOM_CRC::CompareStringsI(Name, G_NameRev) == 0)
        return G_JointTypeRev;
    if (DACOM_CRC::CompareStringsI(Name, G_NameCyl) == 0)
        return G_JointTypeCyl;
    if (DACOM_CRC::CompareStringsI(Name, G_NameSphere) == 0)
        return G_JointTypeSphere;
    if (DACOM_CRC::CompareStringsI(Name, G_NameTrans) == 0)
        return G_JointTypeTrans;
    if (DACOM_CRC::CompareStringsI(Name, G_NameLoose) == 0)
        return G_JointTypeLoose;
    return 0xFFFFFFFF;
}

static void CopyPayloadDwords(unsigned long* Dest, const unsigned long* Source,
                              unsigned long Count)
{
    unsigned long Index = 0;
    while (Index < Count)
    {
        Dest[Index] = Source[Index];
        Index += 1;
    }
}

// Decode the type-specific joint payload from the UTF record.
static void RemapJointPayload(unsigned long Type, const unsigned char* UtfExtra,
                              unsigned long* Payload)
{
    const unsigned long* Source = (const unsigned long*)UtfExtra;
    switch (Type)
    {
    case G_JointTypeFix:
    case G_JointTypeTrans:
    case G_JointTypeLoose:
        CopyPayloadDwords(Payload + 4, Source, 12);
        break;
    case G_JointTypePris:
    case G_JointTypeRev:
        CopyPayloadDwords(Payload + 1, Source, 20);
        break;
    case G_JointTypeCyl:
        CopyPayloadDwords(Payload + 1, Source, 22);
        break;
    case G_JointTypeSphere:
        CopyPayloadDwords(Payload + 1, Source, 15);
        Payload[16] = Source[19];
        Payload[17] = Source[20];
        CopyPayloadDwords(Payload + 19, Source + 15, 4);
        break;
    default:
        break;
    }
}

// PTR_LAB_06621118: type → UTF record size only (not FUN_06625560 apply).
static unsigned long JointUtfRecordSize(unsigned long Type)
{
    if (Type == G_JointTypeFix || Type == G_JointTypeTrans || Type == G_JointTypeLoose)
        return 0xB0;
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
        return 0xD0;
    if (Type == G_JointTypeCyl)
        return 0xD8;
    if (Type == G_JointTypeSphere)
        return 0xD4;
    return 0;
}

// FUN_0661eb30 (thiscall on the handle, RET 4): the first child whose +0x40 equals the CRC
// (+4) of the caller's {text, crc} wrapper, or 0. No tests.
static FArchetypeChild* FindArchetypeChildByCrc(FArchetypeHandle* Handle, unsigned long Crc)
{
    for (std::vector<FArchetypeChild>::iterator Child = Handle->Children.begin();
         Child != Handle->Children.end(); ++Child)
    {
        if (Child->Crc == Crc)
            return &*Child;
    }
    return nullptr;
}

// The record loop of FUN_06620500 for one open Cons file (06620a75..066210c5, the part Ghidra
static void ApplyConsFileRecords(FArchetypeHandle* Handle, IFileSystem* Fs, int File,
                                 unsigned long Type, unsigned long RecordSize, unsigned long FileSize)
{
    unsigned long Left = FileSize / RecordSize;
    while (Left != 0)
    {
        unsigned char Buffer[G_JointUtfMaxBytes];
        unsigned long BytesRead = 0;
        if (Fs->Read((unsigned long)File, Buffer, RecordSize, &BytesRead, 0) != 0 && // +0x14
            BytesRead == RecordSize)
        {
            const char* FirstName = (const char*)Buffer;
            const char* SecondName = (const char*)(Buffer + G_JointUtfNameBytes);
            FArchetypeChild* FirstPart =
                FindArchetypeChildByCrc(Handle, DACOM_CRC::GetCRC32(FirstName));
            FArchetypeChild* SecondPart =
                FindArchetypeChildByCrc(Handle, DACOM_CRC::GetCRC32(SecondName));
            if (FirstPart == nullptr || SecondPart == nullptr)
            {
                char Message[0x2000];
                _snprintf(Message, sizeof(Message), "Joint %s<->%s not found!", FirstName,
                          SecondName);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x389, "WARNING", Message);
            }
            else
            {
                // The record is a zeroed stack temporary (06620adb..aff) filled in place and
                // appended with insert(end, 1, record) (FUN_066237a0, 0662105d..72).
                FConstraintRecord Record = {};
                Record.ChildPart = SecondPart;
                Record.ParentPart = FirstPart;
                Record.Payload[0] = Type;
                RemapJointPayload(Type, Buffer + G_JointUtfHeaderBytes, Record.Payload);
                Handle->Constraints.push_back(Record);
            }
        }
        Left -= 1;
    }
}

// The Cons pass of FUN_06620500 (0662089c..06621109). OpenNode("Cons") (DAT_0662a788); 0:
static void ParseConsConstraints(FArchetypeHandle* Handle, IFileSystem* Fs)
{
    if (Fs->OpenNode(G_NameCons) == 0) // +0x60
        return;
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar, // +0x48
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            if (FindData.cFileName[0] == '.')
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.Field30 = (unsigned long)FindHandle;
            const int File = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
            if (File == -1)
                continue;
            const unsigned long FileSize = Fs->GetSize((unsigned long)File, 0); // +0x28
            const unsigned long Type = JointTypeFromName(FindData.cFileName);
            const unsigned long RecordSize = JointUtfRecordSize(Type);
            if (Type == 0xFFFFFFFF)
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x376, "WARNING",
                             "Engine: unknown joint type, probably random garbage");
            ApplyConsFileRecords(Handle, Fs, File, Type, RecordSize, FileSize);
            Fs->CloseFile((unsigned long)File); // +0x10
        } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                            &FindData) != 0);
        Fs->FindClose((unsigned long)FindHandle); // +0x50
    }
    Fs->OpenNode(G_NameParentDot); // +0x60
}

// 0662061a / 06620857: the text both FUN_06620500 part failures push (0662a798).
static const char* const G_CmpndPartFailText =
    "Engine: create_compound_archetype: unable to create part archetype, ignoring...";

// FUN_06620500, thiscall on the engine, RET 0xC (Handle, CmpndFs, ParentFs).
static GENRESULT ParseCmpndChildren(FEngineObject* Engine, FArchetypeHandle* Handle, void* CmpndFs,
                                   void* ParentFs)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return GR_E_FAIL;
    IFileSystem* Fs = AsFileSystem(CmpndFs);

    std::size_t Count = Handle->Children.size();
    FArchetypeChild* Child = AppendArchetypeChild(Handle);
    if (LinkCmpndPartHandle(Engine, G_NameRoot, CmpndFs, 0xFFFFFFFF, ParentFs, Child) < 0)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x331, "WARNING", G_CmpndPartFailText);
        ShrinkArchetypeChildren(Handle, Count);
    }

    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NamePartStar, // +0x48
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            Count = Handle->Children.size();
            Child = AppendArchetypeChild(Handle);
            if (LinkCmpndPartHandle(Engine, FindData.cFileName, CmpndFs,
                                    (unsigned long)FindHandle, ParentFs, Child) < 0)
            {
                ShrinkArchetypeChildren(Handle, Count);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x345, "WARNING",
                             G_CmpndPartFailText);
            }
        } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                            &FindData) != 0);
        Fs->FindClose((unsigned long)FindHandle); // +0x50
    }

    ParseConsConstraints(Handle, Fs);
    return GR_OK;
}

// LAB_06621150, IEngine +0x30, RET 0xC (Name, FileSystem).
static FArchetypeHandle* DACOM_API EngineCreateArchetypeHandle(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, G_NameCmpnd, nullptr);
    void* CmpndFs = nullptr;
    if (Name->Crc == 0)
        return G_InvalidArchetype;

    FArchetypeHandle* Existing = LookupArchetype(This, Name);
    if (Existing != G_InvalidArchetype)
    {
        This->AddRefHandle(Existing); // +0x38
        return Existing;
    }
    if (FileSystem == nullptr)
        return G_InvalidArchetype;

    FArchetypeHandle* Handle = (FArchetypeHandle*)(This->CreateCompoundArchetype(Name)); // +0x2C
    if (Handle == G_InvalidArchetype)
        return G_InvalidArchetype;

    if (AsFileSystem(FileSystem)->CreateInstance((AGGDESC*)(&Desc), &CmpndFs) < 0 ||
        ParseCmpndChildren(This, Handle, CmpndFs, FileSystem) < 0)
        FanoutIecCreateArchetype(This, Handle, FileSystem);

    if (CmpndFs != nullptr)
        ((IDAComponent*)CmpndFs)->Release();
    return Handle;
}

// LAB_06621850 / IEngine+0x40 RET 0xC, listing 06621850-06621898: (Out, Handle) -- Out at
// [ESP+8], Handle at [ESP+0xC]. No null test on Out.
static FArchetypeName* DACOM_API EngineGetArchetypeName(FEngineObject* This, FArchetypeName* Out,
                                                       FArchetypeHandle* Handle)
{
    (void)This;
    if (Handle == G_InvalidArchetype || Handle == nullptr)   // 06621855 / 0662185a
    {
        Out->Text = nullptr;                                 // 06621883
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);             // 06621889
        return Out;
    }
    // 0662185e..0662186b: the handle's string _Ptr (+4), or the std::string empty
    // string (MSVCP60 _Nullstr) when it has none.
    Out->Text = Handle->Name.c_str();
    Out->Crc = Handle->Crc;                                  // 0662186d [handle+0x10]
    return Out;
}

// LAB_06621530 / IEngine+0x44 RET 8: lookup + slot 14.
static FArchetypeHandle* DACOM_API EngineFindArchetype(FEngineObject* This, FArchetypeName* Name)
{
    FArchetypeHandle* Handle = LookupArchetype(This, Name);
    if (Handle != G_InvalidArchetype)
        CallEngineSlot14(This, Handle);
    return Handle;
}

// LAB_06622400 / IEngine+0x4C RET 8: AL=1 if child count != 0.
static unsigned long DACOM_API EngineIsCompoundArchetype(FEngineObject* This,
                                                        FArchetypeHandle* Handle)
{
    (void)This;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    return Handle->Children.size() != 0 ? 1 : 0;
}

// Packed 3x3 orientation followed by translation (12 floats).
static float G_IdentityMatrix[12] = {
    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

static void InitIdentity3x4(float* Matrix)
{
    std::memcpy(Matrix, G_IdentityMatrix, sizeof(G_IdentityMatrix));
}

static void InitInstanceMatrix(FEngineInstance* Instance)
{
    InitIdentity3x4(Instance->Matrix);
}

static FEngineInstance* const G_InvalidInstance =
    (FEngineInstance*)G_InvalidEngineHandle;

static bool IsValidInstance(FEngineInstance* Instance)
{
    return Instance != G_InvalidInstance && Instance != nullptr;
}

// Engine-owned transform; setters retain the existing return policy.

// LAB_0661f340 (slot 0, RET 8): 3x3 identity (1.0 at +4 / +0x14 / +0x24, 0 elsewhere),
// translation 0, extra 0, extra vector 0. +0x44 (user) is NOT touched.
GENRESULT DACOM_API FInstanceTransform::Init(void* Instance)
{
    (void)Instance;
    InitIdentity3x4(Matrix);
    Extra = 0.0f;
    ExtraVec[0] = 0.0f;
    ExtraVec[1] = 0.0f;
    ExtraVec[2] = 0.0f;
    return GR_OK;
}

// LAB_06612160 (slots 1 and 2, RET 8): no body.
GENRESULT DACOM_API FInstanceTransform::AddRef(void* Instance)
{
    (void)Instance;
    return GR_OK;
}

GENRESULT DACOM_API FInstanceTransform::Release(void* Instance)
{
    (void)Instance;
    return GR_OK;
}

// LAB_0661f380 (slot 3, RET 0xC): three dwords to +0x28.
GENRESULT DACOM_API FInstanceTransform::SetPosition(void* Instance, const float* Position)
{
    (void)Instance;
    Matrix[9] = Position[0];
    Matrix[10] = Position[1];
    Matrix[11] = Position[2];
    return GR_OK;
}

// LAB_0661f3a0 (slot 4, RET 8): this + 0x28.
float* DACOM_API FInstanceTransform::GetPosition(void* Instance)
{
    (void)Instance;
    return Matrix + 9;
}

// LAB_0661f3b0 (slot 5, RET 0xC): MOVSD x9 to +4.
GENRESULT DACOM_API FInstanceTransform::SetOrientation(void* Instance, const float* Rows)
{
    (void)Instance;
    std::memcpy(Matrix, Rows, 9 * sizeof(float));
    return GR_OK;
}

// LAB_0661f3d0 (slots 6 and 8, RET 8): this + 4.
float* DACOM_API FInstanceTransform::GetOrientation(void* Instance)
{
    (void)Instance;
    return Matrix;
}

float* DACOM_API FInstanceTransform::GetMatrix(void* Instance)
{
    (void)Instance;
    return Matrix;
}

// LAB_0661f3e0 (slot 7, RET 0xC): MOVSD x0xC to +4.
GENRESULT DACOM_API FInstanceTransform::SetMatrix(void* Instance, const float* NewMatrix)
{
    (void)Instance;
    std::memcpy(Matrix, NewMatrix, 12 * sizeof(float));
    return GR_OK;
}

// LAB_0661f400 (slot 9, RET 0x10): *OutExtra = +0x34, OutVec = +0x38..+0x40.
GENRESULT DACOM_API FInstanceTransform::GetExtra(void* Instance, float* OutExtra, float* OutVec)
{
    (void)Instance;
    *OutExtra = Extra;
    OutVec[0] = ExtraVec[0];
    OutVec[1] = ExtraVec[1];
    OutVec[2] = ExtraVec[2];
    return GR_OK;
}

// LAB_0661f430 (slot 10, RET 0x10): +0x34 = Extra (FLD/FSTP float), Vec to +0x38.
GENRESULT DACOM_API FInstanceTransform::SetExtra(void* Instance, float NewExtra, const float* Vec)
{
    (void)Instance;
    Extra = NewExtra;
    ExtraVec[0] = Vec[0];
    ExtraVec[1] = Vec[1];
    ExtraVec[2] = Vec[2];
    return GR_OK;
}

// LAB_0661f460 (slot 11, RET 0xC): +0x44 = User.
GENRESULT DACOM_API FInstanceTransform::SetUser(void* Instance, unsigned long NewUser)
{
    (void)Instance;
    User = NewUser;
    return GR_OK;
}

// LAB_0661f470 (slot 12, RET 8): +0x44.
unsigned long DACOM_API FInstanceTransform::GetUser(void* Instance)
{
    (void)Instance;
    return User;
}

// LAB_0661f320 (slot 13, RET 8): XOR AL,AL.
unsigned long DACOM_API FInstanceTransform::MarkWorldDirty(void* Instance) // formerly Slot13
{
    (void)Instance;
    return 0;
}

// FUN_0661f2f0 (free list DAT_0662b114) + FUN_0661f310 (ctor: +0x44 = 0, vtable 066295d0)
// as 06621905..06621918 uses them; the rest of the block is left as the allocator hands it
// out (slot 0 Init fills it). A null block yields 0.
static FInstanceTransform* AllocInstanceTransform()
{
    void* Block = FreeListAlloc(&G_TransformPool);
    if (Block == nullptr)
        return nullptr;
    return new (Block) FInstanceTransform;
}

// FUN_0661f330 (dtor: vtable back to 066295d0) + FUN_0661f300 (block back to DAT_0662b114).
static void FreeInstanceTransform(FInstanceTransform* Transform)
{
    Transform->~FInstanceTransform();
    FreeListFree(&G_TransformPool, Transform);
}

static void CopyTransformState(IInstanceTransform* Dest, IInstanceTransform* Source,
                               FEngineInstance* Instance)
{
    // 06621935..06621975: get-matrix / set-matrix, get-extra / set-extra, get-user /
    // set-user, in that order and always through the interface on both sides. No tests (the
    // two stack locals of the extra pair are uninitialised in retail; the port zeroes them).
    Dest->SetMatrix(Instance, Source->GetMatrix(Instance));
    float Extra = 0.0f;
    float ExtraVec[3] = {0.0f, 0.0f, 0.0f};
    Source->GetExtra(Instance, &Extra, ExtraVec);
    Dest->SetExtra(Instance, Extra, ExtraVec);
    const unsigned long User = Source->GetUser(Instance);
    Dest->SetUser(Instance, User);
}

// FUN_0661eb80 thiscall: zero lists/matrix extras, ident 3x3 @ +0x2c/3c/4c, Relative[0]=-1 then
// 0x17 zeros, JointState ident W, bWorldDirty=1, Extent=-1, +8=param.
static FEngineInstance* EngineInstanceInit(FEngineInstance* This, FArchetypeHandle* Handle)
{
    if (This == nullptr)
        return nullptr;
    std::memset(This, 0, sizeof(FEngineInstance));
    This->Archetype = Handle;
    This->Extent = -1.0f;
    This->bWorldDirty = 1;
    This->JointState[3] = 1.0f;
    This->Relative[0] = 0xFFFFFFFF;
    InitInstanceMatrix(This);
    return This;
}

// FUN_0661eb60 (pool DAT_0662b158, 0xEC) + FUN_0661eb80 (ctor) as 066216a5..066216c2 uses
// them: a null allocation skips the ctor and yields 0 (XOR EDI,EDI), which retail then
// dereferences -- no failure branch.
static FEngineInstance* AllocEngineInstance(FArchetypeHandle* Handle)
{
    FEngineInstance* Instance = (FEngineInstance*)(FreeListAlloc(&G_InstancePool));
    if (Instance != nullptr)
        EngineInstanceInit(Instance, Handle);
    return Instance;
}

// The instance-root map key: the instance pointer as a signed int (FUN_06612e70, JGE).
static long EngineInstanceKey(FEngineInstance* Instance)
{
    return (long)((intptr_t)Instance);
}

// FUN_06623ac0 with the pair {Instance, Instance} and the cache update every caller does
// after it (06622a96..abe, 06622107..22, 06622261..89): insert-if-absent; when the result
// is not end() it becomes the cached node.
static void InsertRootInstance(FEngineObject* Engine, FEngineInstance* Instance)
{
    FEngineInstanceMap::iterator It =
        Engine->Instances.insert(FEngineInstanceMap::value_type(EngineInstanceKey(Instance), Instance))
            .first;
    if (It != Engine->Instances.end())
    {
        Engine->CachedInstance = It;
        Engine->bInstanceCacheValid = 1;
    }
}

// The erase every caller inlines (0662177f..066217f4, 0662205d..066220c4,
// 066221e3..0662224c): clear the cache byte when the cached node's key is Instance, then
// erase equal_range(Instance) (FUN_06625220 / FUN_06624ca0 / FUN_06625500 / FUN_06623200).
static void EraseRootInstance(FEngineObject* Engine, FEngineInstance* Instance)
{
    const long Key = EngineInstanceKey(Instance);
    if (Engine->bInstanceCacheValid != 0 && Engine->CachedInstance->first == Key)
        Engine->bInstanceCacheValid = 0;
    Engine->Instances.erase(Key);
}

static unsigned char UnlinkInstanceChild(FEngineInstance* Parent, FEngineInstance* Node);
static unsigned char EngineInvalidateExtentChain(FEngineInstance* This, unsigned char bFromThis);


// FUN_06625560: this=inst+0x5C, dest=inst+0x2C, force. Jumptable PTR_LAB_0662aa84.
// Relative: [0] type, [1..3] rest pos, [4..6] T @ +0x10, [7..15] 3x3 @ +0x1C, [16..18] axis @ +0x40.
// DAT_06629510 = 0.5f (half-angle). 3DMath +0x20/+0x2C/+0x54 only — no invented IK.
static const float G_JointHalfAngle = 0.5f;

static void CopyRelative3x3(float* Dest, const float* Rel)
{
    std::memcpy(Dest, Rel + 7, 9 * sizeof(float));
}

static void CopyRelativeTranslation(float* Dest, const float* Rel)
{
    Dest[9] = Rel[4];
    Dest[10] = Rel[5];
    Dest[11] = Rel[6];
}

static void AddVec3(float* Dest, const float* A, const float* B)
{
    Dest[0] = A[0] + B[0];
    Dest[1] = A[1] + B[1];
    Dest[2] = A[2] + B[2];
}

static void SubVec3(float* Dest, const float* A, const float* B)
{
    Dest[0] = A[0] - B[0];
    Dest[1] = A[1] - B[1];
    Dest[2] = A[2] - B[2];
}

// Quaternion order is W, X, Y, Z; preserve the float spill sites.
static void AxisAngleToQuat(float* OutQuat, const float* Axis, float Angle)
{
    const double Half = (double)Angle * (double)G_JointHalfAngle;
    const double Sin = std::sin(Half);
    const double X = Axis[0];
    const double Y = Axis[1];
    const double Z = Axis[2];
    const double Len = std::sqrt(X * X + Y * Y + Z * Z);
    const float K = (float)(Sin / Len);
    OutQuat[0] = (float)(std::cos(Half));
    OutQuat[1] = (float)((double)K * X);
    OutQuat[2] = (float)((double)K * Y);
    OutQuat[3] = (float)((double)K * Z);
}

// Preserve separate float rounding for Y/Z translation intermediates.
static void SlideTranslation(float* OutT, const float* Rel, float Slide, const float* V)
{
    const double S = Slide;
    const double Ax = (double)(Rel[16]) * S;
    const float Ty = (float)(S * (double)(Rel[17]));
    const float Tz = (float)(S * (double)(Rel[18]));
    const float Pz = (float)((double)Tz + (double)(Rel[3]));
    OutT[0] = (float)((Ax + (double)(Rel[1])) - (double)(V[0]));
    OutT[1] = (float)(((double)Ty + (double)(Rel[2])) -
                                 (double)(V[1]));
    OutT[2] = (float)((double)Pz - (double)(V[2]));
}

static void MathMulMatrix3(I3DMathEngine* Math, float* Out9, const float* Left, const float* Right)
{
    MathMulMatrix(Math, Out9, Left, Right);
}

static void MathTransformVec(I3DMathEngine* Math, float* Out3, const float* Matrix, const float* In3)
{
    MathTransformMatrix(Math, Out3, Matrix, In3);
}

// FUN_0661ed40, thiscall (Instance) RET 0x8 (OutExtent, OutVec), 0661ed40..56: the transform
// at +4, slot 9 GetExtra(Instance, OutExtent, OutVec). Nothing else -- until 2026-09-30 the
// port zero-filled both outputs first and returned early on a null Instance or Transform.
static void InstanceTransformExtra(FEngineInstance* Instance, float* OutExtent, float* OutVec)
{
    Instance->Transform->GetExtra(Instance, OutExtent, OutVec);
}

// DAT_0662b1b0: the 3DMathEngine every joint helper re-checks before each call
// (`if (!g) g = FUN_06628190()`, e.g. 0662566b / 0662568e / 066256c1).
static I3DMathEngine* G_JointMath = nullptr;

static I3DMathEngine* JointMath()
{
    if (G_JointMath == nullptr)
        G_JointMath = Create3DMathEngine();
    return G_JointMath;
}

// Rev / Sphere tail: v = math +0x2C (Dest 3x3, Rel[4..6]); T = Rel[1..3] - v (float FSUBs).
static void ApplyRestTranslation(float* Dest, const float* Rel)
{
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SubVec3(Dest + 9, Rel + 1, Rotated);
}

static void ApplyFixJoint(float* Dest, const float* Rel)
{
    CopyRelative3x3(Dest, Rel);
    CopyRelativeTranslation(Dest, Rel);
}

static void ApplyTransJoint(float* Dest, const float* Rel, const float* JointState)
{
    CopyRelative3x3(Dest, Rel);
    AddVec3(Dest + 9, JointState, Rel + 4);
}

// LAB_06625730 (Pris): Dest 3x3 = Rel 3x3 (MOVSD x9); v = math +0x2C (Dest, Rel[4..6]);
// T = SlideTranslation(State[0]).
static void ApplyPrisJoint(float* Dest, const float* Rel, const float* JointState)
{
    CopyRelative3x3(Dest, Rel);
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SlideTranslation(Dest + 9, Rel, JointState[0], Rotated);
}

// LAB_066255e0 (Rev): q = AxisAngleToQuat(axis Rel[16..18], State[0]); R = math +0x54 (q);
// Dest 3x3 = math +0x20 (R, Rel 3x3) -- the rotation on the LEFT (066256a3..b3); then the
// rest translation.
static void ApplyRevJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    float Rot[9];
    AxisAngleToQuat(Quat, Rel + 16, JointState[0]);
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    ApplyRestTranslation(Dest, Rel);
}

// LAB_066257f0 (Cyl): q from State[1] (FUN_0661c270 stores x/y/z, W kept aside);
static void ApplyCylJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    float Rot[9];
    AxisAngleToQuat(Quat, Rel + 16, JointState[1]);
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rel + 7, Rot);
    std::memcpy(Dest, Product, sizeof(Product));
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SlideTranslation(Dest + 9, Rel, JointState[0], Rotated);
}

// LAB_06625980 (Sphere): q = State[3..6] (copied to a local); R = math +0x54 (q);
// Dest 3x3 = math +0x20 (R, Rel 3x3); then the rest translation.
static void ApplySphereJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    std::memcpy(Quat, JointState + 3, sizeof(Quat));
    float Rot[9];
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    ApplyRestTranslation(Dest, Rel);
}

// LAB_06625af0 (Loose): q = State[3..6]; R = math +0x54 (q); Dest 3x3 = math +0x20 (R, Rel 3x3);
// T = State[0..2] + Rel[4..6] (float FADDs).
static void ApplyLooseJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    std::memcpy(Quat, JointState + 3, sizeof(Quat));
    float Rot[9];
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    AddVec3(Dest + 9, JointState, Rel + 4);
}

// FUN_06625560 (thiscall on inst+0x5C, RET 8 (Dest, bForce); only caller FUN_0661f250 with
static unsigned long ApplyJointToLocal(FEngineInstance* Instance, unsigned char bForce)
{
    if (Instance->bWorldDirty == 0 && bForce == 0)
        return 0;
    Instance->bWorldDirty = 0;
    const unsigned long Type = Instance->Relative[0];
    if (Type == 0xFFFFFFFF)
        return 0;

    float* Dest = Instance->Matrix;
    const float* Rel = (const float*)(Instance->Relative);
    const float* State = Instance->JointState;
    if (Type == G_JointTypeFix)
        ApplyFixJoint(Dest, Rel);
    else if (Type == G_JointTypePris)
        ApplyPrisJoint(Dest, Rel, State);
    else if (Type == G_JointTypeRev)
        ApplyRevJoint(Dest, Rel, State);
    else if (Type == G_JointTypeCyl)
        ApplyCylJoint(Dest, Rel, State);
    else if (Type == G_JointTypeSphere)
        ApplySphereJoint(Dest, Rel, State);
    else if (Type == G_JointTypeTrans)
        ApplyTransJoint(Dest, Rel, State);
    else if (Type == G_JointTypeLoose)
        ApplyLooseJoint(Dest, Rel, State);
    else
        return 0;
    return 1;
}

// FUN_0661ec90: memcpy 0x17 dwords from constraint+8 → inst+0x5C, then link.
static unsigned char ConnectInstanceChild(FEngineInstance* Parent, FEngineInstance* Child,
                                          const unsigned long* Payload)
{
    Child->bWorldDirty = 1;                                             // 0661eca4 [child+0xD4]
    std::memcpy(Child->Relative, Payload, sizeof(Child->Relative));     // 0661eca8 REP MOVSD 0x17
    FEngineInstance* Last = Parent->LastChild;                          // 0661ecaa
    Parent->LastChild = Child;                                          // 0661ecad
    if (Last == nullptr)
        Parent->FirstChild = Child;                                     // 0661ecb4
    else
        Last->NextSibling = Child;                                      // 0661ecb9
    Child->NextSibling = nullptr;                                       // 0661ecbc
    Child->PrevSibling = Last;                                          // 0661ecc3
    Parent->ChildCount += 1;                                            // 0661ecce
    Child->Parent = Parent;                                             // 0661ecd1
    EngineInvalidateExtentChain(Parent, 1);                             // 0661ecd4
    return 1;                                                           // 0661ecda MOV AL,1
}

// FUN_0661ed60 RET 4: flag==0 start at [this+0x10]; walk +0x10 while +0xD8 != -1 (dword
// compare with 0xBF800000) write -1. No test of this. AL = 1.
static unsigned char EngineInvalidateExtentChain(FEngineInstance* This, unsigned char bFromThis)
{
    FEngineInstance* Cursor = This;
    if (bFromThis == 0)
        Cursor = Cursor->Parent;
    while (Cursor != nullptr)
    {
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Cursor->Extent, sizeof(Bits));
        if (Bits == 0xBF800000)
            break;
        Cursor->Extent = -1.0f;
        Cursor = Cursor->Parent;
    }
    return 1;
}

// FUN_0661ece0 (thiscall on the instance, RET 0xC; its only caller is IEngine slot 33
static void EngineSetExtentAndInvalidate(FEngineInstance* This, float Extent, const float* Vec,
                                         unsigned char bXform)
{
    if (bXform != 0)
    {
        This->Transform->SetExtra(This, Extent, Vec);
        EngineInvalidateExtentChain(This, bXform);
        return;
    }
    This->Extent = Extent;
    This->ExtentVec[0] = Vec[0];
    This->ExtentVec[1] = Vec[1];
    This->ExtentVec[2] = Vec[2];
    EngineInvalidateExtentChain(This, 0);
}

// FUN_0661ec30 (thiscall, this = PARENT, RET 4): unlink Node from Parent's child list --
static unsigned char UnlinkInstanceChild(FEngineInstance* Parent, FEngineInstance* Node)
{
    if (Node->PrevSibling == nullptr)
        Parent->FirstChild = Node->NextSibling;
    else
        Node->PrevSibling->NextSibling = Node->NextSibling;
    if (Node->NextSibling == nullptr)
        Parent->LastChild = Node->PrevSibling;
    else
        Node->NextSibling->PrevSibling = Node->PrevSibling;
    Parent->ChildCount -= 1;
    Node->Parent = nullptr;
    Node->NextSibling = nullptr;
    Node->PrevSibling = nullptr;
    return 1;
}

static GENRESULT DACOM_API EngineSetInstanceTransform(FEngineObject* This, FEngineInstance* Instance,
                                                     IInstanceTransform* NewTransform);

// FUN_06621630's leaf path, 06621698..0662170c. No failure branch and no instance-map insert
static FEngineInstance* EngineCreateInstanceSimple(FEngineObject* This, FArchetypeHandle* Handle,
                                                  IInstanceTransform* Transform)
{
    This->AddRefHandle(Handle);
    FEngineInstance* Instance = AllocEngineInstance(Handle);
    This->SetInstanceTransform(Instance, Transform);
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->CreateInstance(Instance, Handle);
    Instance->Transform->AddRef(Instance);
    return Instance;
}

static FEngineInstance* DACOM_API EngineCreateInstance(FEngineObject* This,
                                                      FArchetypeHandle* Handle, void* Transform);

// FUN_06622860 (thiscall, this = engine) RET 8, listing 06622860-06622b5a. Called only from
static FEngineInstance* EngineCreateInstanceTree(FEngineObject* This, FArchetypeHandle* Handle,
                                                IInstanceTransform* Transform)
{
    // 06622875..0662287c: -1 / null handle -> OR EAX,-1.
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return G_InvalidInstance;

    std::map<unsigned long, FEngineInstance*> Parts; // 0662289e FUN_06623730, dtor 06622ac2..

    // 066228a9 / 066228bd / 06622908: the caller's transform (EBP) goes to every child
    // until the first one is created successfully; EBP is then zeroed, so every later part
    // gets its own engine-owned transform. A failed child (066228c8 JZ) keeps EBP.
    IInstanceTransform* ChildTransform = Transform;
    FArchetypeChild* Child = Handle->Children.data();
    while (Child != Handle->Children.data() + Handle->Children.size()) // 066228b0 / 06622911 CMP EDI,end
    {
        // 066228b8..066228c0: IEngine slot 23 on child+0x44, no filtering -- slot 23 itself
        // answers -1 for a -1/null handle.
        FEngineInstance* ChildInst =
            (FEngineInstance*)(This->CreateInstance(Child->Handle, ChildTransform));
        if (ChildInst != G_InvalidInstance) // 066228c5 CMP ESI,-1
        {
            ChildInst->ParentArchetype = Handle; // 066228d5 [inst+0xC]
            ChildInst->PartChild = Child;        // 066228d8 [inst+0xE8]
            // 066228de..06622900: insert {child.Crc, 0}, then node->second = instance
            // (overwrites an existing entry with the same CRC, as operator[] does).
            Parts[Child->Crc] = ChildInst;
            This->AddRefHandle(Handle);          // 06622905 IEngine slot 14 (+0x38)
            ChildTransform = nullptr;            // 06622908 XOR EBP,EBP
        }
        Child += 1;                              // 0662290e ADD EDI,0x48
    }

    FEngineInstance* Root = nullptr;             // 06622922 XOR EBP,EBP
    FConstraintRecord* Constraint = Handle->Constraints.data();
    while (Constraint != Handle->Constraints.data() + Handle->Constraints.size()) // 06622924 / 06622a74
    {
        // 06622930..0662293f: a record with a null child or parent part is skipped silently.
        if (Constraint->ChildPart != nullptr && Constraint->ParentPart != nullptr)
        {
            std::map<unsigned long, FEngineInstance*>::iterator ChildIt =
                Parts.find(Constraint->ChildPart->Crc); // 06622952
            if (ChildIt == Parts.end())
            {
                // 06622963..066229b0: IEngine slot 16 for the archetype name, "" when null.
                FEngineName ArchName;
                const char* ArchText = This->GetArchetypeName(&ArchName, Handle)->Text;
                if (ArchText == nullptr)
                    ArchText = "";               // DAT_0662ace4
                char Message[0x2000];
                _snprintf(Message, sizeof(Message),
                          "Unable to find child part '%s' in archetype '%s'",
                          Constraint->ChildPart->Name, ArchText);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8D7, "WARNING", Message);
            }
            else
            {
                FEngineInstance* ChildInst = ChildIt->second; // 066229bd
                std::map<unsigned long, FEngineInstance*>::iterator ParentIt =
                    Parts.find(Constraint->ParentPart->Crc); // 066229cd
                if (ParentIt == Parts.end())
                {
                    // 066229de..06622a2e
                    FEngineName ArchName;
                    const char* ArchText = This->GetArchetypeName(&ArchName, Handle)->Text;
                    if (ArchText == nullptr)
                        ArchText = "";           // DAT_0662ace4
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message),
                              "Unable to find parent part '%s' in archetype '%s'",
                              Constraint->ParentPart->Name, ArchText);
                    EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8E0, "WARNING", Message);
                }
                else
                {
                    // 06622a35: EBP = parent instance, kept even when the connect fails.
                    Root = ParentIt->second;
                    if (ConnectInstanceChild(Root, ChildInst, Constraint->Payload) == 0)
                        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8EB, "WARNING",
                                     "Unable to connect child to parent"); // 06622a48..06622a68
                }
            }
        }
        Constraint += 1;                         // 06622a71 ADD ESI,0x64
    }

    if (Root != nullptr)
    {
        // 06622a80..06622a8b: climb +0x10 to the top of the last connected parent's tree.
        while (Root->Parent != nullptr)
            Root = Root->Parent;
    }
    else
    {
        // 06622a8d..06622a93: no constraint connected anything -> the part with the lowest
        if (Parts.empty())
            return G_InvalidInstance;
        Root = Parts.begin()->second;
    }

    // 06622a96..06622abe: insert {Root, Root} into the engine's instance-root map (+0x10) and
    // cache the result (+0x28 / byte +0x2C) when it is not end().
    InsertRootInstance(This, Root);
    return Root;                                 // 06622b4e MOV EAX,EBP
}

// FUN_06621630 / IEngine+0x5C RET 0xC: children → FUN_06622860; else alloc + slot 28 + IEC+0x20.
static FEngineInstance* DACOM_API EngineCreateInstance(FEngineObject* This,
                                                      FArchetypeHandle* Handle, void* Transform)
{
    // 06621635..3d: -1 / 0 -> -1. 06621647..68: a non-empty child vector ((end - begin) / 0x48,
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return G_InvalidInstance;
    IInstanceTransform* Xf = (IInstanceTransform*)Transform;
    if (Handle->Children.size() != 0)
    {
        FEngineInstance* Root = EngineCreateInstanceTree(This, Handle, Xf);
        if (Root == G_InvalidInstance)
            return Root;
        Root->Transform->AddRef(Root);
        return Root;
    }
    return EngineCreateInstanceSimple(This, Handle, Xf);
}

// FUN_06621710 (thiscall engine, RET 4), in retail order:
static void EngineDestroyInstanceBody(FEngineObject* This, FEngineInstance* Instance)
{
    while (Instance->FirstChild != nullptr)
        EngineDestroyInstanceBody(This, Instance->FirstChild);
    Instance->Transform->Release(Instance);
    if (Instance->Parent != nullptr)
        UnlinkInstanceChild(Instance->Parent, Instance);
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DestroyInstance(Instance);
    if ((Instance->Flags & G_InstanceFlagOwnedTransform) != 0 && Instance->Transform != nullptr)
        FreeInstanceTransform((FInstanceTransform*)(Instance->Transform)); // flag 2: ours
    Instance->Transform = nullptr;
    EraseRootInstance(This, Instance);
    This->ReleaseHandle(Instance->Archetype);
    if (Instance->ParentArchetype != nullptr)
        This->ReleaseHandle(Instance->ParentArchetype);
    FreeListFree(&G_InstancePool, Instance);
}

// LAB_06621830 / IEngine+0x60 (stdcall RET 8): -1 or 0 -> return with EAX still holding the
// argument (06621830 loads it into EAX and never overwrites it on that path); otherwise
// FUN_06621710 and EAX = 0.
static GENRESULT DACOM_API EngineDestroyInstance(FEngineObject* This, FEngineInstance* Instance)
{
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return (GENRESULT)((intptr_t)Instance);
    EngineDestroyInstanceBody(This, Instance);
    return GR_OK;
}

// LAB_066215d0 / IEngine+0x58 RET 0x10: slot 12, slot 23 (+transform), slot 15.
static FEngineInstance* DACOM_API EngineCreateInstanceFromName(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem, void* Transform)
{
    FArchetypeHandle* Handle = EngineCreateArchetypeHandle(This, Name, FileSystem);
    if (Handle == G_InvalidArchetype)
        return G_InvalidInstance;
    FEngineInstance* Instance = EngineCreateInstance(This, Handle, Transform);
    EngineReleaseHandle(This, Handle);
    return Instance;
}

// LAB_06622360 / IEngine +0x50 (RET 0x10): the argument is an archetype handle. -1 -> -1
static GENRESULT DACOM_API EngineEnumerateArchetypeConstraints(FEngineObject* This,
                                                              FArchetypeHandle* Handle,
                                                              int(__cdecl* Callback)(void*, void*,
                                                                                      void*, void*),
                                                              void* Context)
{
    (void)This;
    if (Handle == G_InvalidArchetype)
        return (GENRESULT)(-1);
    FConstraintRecord* Record = Handle->Constraints.data();
    FConstraintRecord* const End = Record + Handle->Constraints.size();
    while (Record != End)
    {
        if ((unsigned char)(Callback(Record->ParentPart->Handle,
                                                Record->ChildPart->Handle, &Record->Payload[0],
                                                Context)) == 0)
            break;
        Record += 1;
    }
    return GR_OK;
}

// FUN_066223b0 / IEngine +0x54 (RET 0x10): -1 -> -1 (066223b5..bd, OR EAX,EDI); nothing else
static GENRESULT DACOM_API EngineEnumerateArchetypeChildren(FEngineObject* This,
                                                           FArchetypeHandle* Handle,
                                                           int(__cdecl* Callback)(void*, void*, int,
                                                                                   void*),
                                                           void* Context)
{
    (void)This;
    if (Handle == G_InvalidArchetype)
        return (GENRESULT)(-1);
    FArchetypeChild* Child = Handle->Children.data();
    FArchetypeChild* const End = Child + Handle->Children.size();
    while (Child != End)
    {
        if ((unsigned char)(Callback(Handle, Child->Handle, 0, Context)) == 0)
            break;
        Child += 1;
    }
    return GR_OK;
}

// FUN_066218a0 / IEngine+0x64 RET 8: inst+8, slot 14.
static FArchetypeHandle* DACOM_API EngineGetInstanceArchetype(FEngineObject* This,
                                                             FEngineInstance* Instance)
{
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return G_InvalidArchetype;
    EngineAddRefHandle(This, Instance->Archetype);
    return Instance->Archetype;
}

// LAB_06622530 / IEngine+0x68 RET 0xC: stdcall (this, out, inst).
// inst invalid or +0xC==0 → empty name; else Text=inst+0xE8, Crc=[that+0x40].
static FArchetypeName* DACOM_API EngineGetInstanceName(FEngineObject* This, FArchetypeName* Out,
                                                      FEngineInstance* Instance)
{
    (void)This;
    if (Out == nullptr)
        return nullptr;
    if (!IsValidInstance(Instance) || Instance->ParentArchetype == nullptr)
    {
        Out->Text = nullptr;
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
        return Out;
    }
    Out->Text = (char*)(Instance->PartChild);
    if (Instance->PartChild != nullptr)
        Out->Crc = Instance->PartChild->Crc;
    else
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
    return Out;
}

// FUN_066219d0 / IEngine+0x78 RET 0x10: IEC+0x2C (inst, iid, out).
static GENRESULT DACOM_API EngineQueryInstance(FEngineObject* This, FEngineInstance* Instance,
                                              const char* Iid, void** Out)
{
    // 066219d0: *Out = 0 (no test); instance -1 (only) or Iid null -> -2; IEC +0x2C (inst,
    // iid, out) over +0x54, the first hr >= 0 -> 0; none -> -1.
    *Out = nullptr;
    if (Instance == G_InvalidInstance || Iid == nullptr)
        return GR_INVALID_PARMS;
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        const GENRESULT Result = AsEngineComponent(Node->Object)->QueryInstance(Instance, Iid, Out);
        if (Result >= 0)
            return GR_OK;
    }
    return (GENRESULT)(-1);
}

// LAB_066218d0 / IEngine +0x70 (RET 0xC), in retail order:
static GENRESULT DACOM_API EngineSetInstanceTransform(FEngineObject* This, FEngineInstance* Instance,
                                                     IInstanceTransform* NewTransform)
{
    (void)This;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return GR_OK;
    const unsigned long OldFlags = Instance->Flags;
    IInstanceTransform* Old = Instance->Transform;
    IInstanceTransform* Attached = NewTransform;
    if (Attached == nullptr)
    {
        Instance->Flags = OldFlags | G_InstanceFlagOwnedTransform;
        Attached = AllocInstanceTransform();
    }
    else
    {
        Instance->Flags = OldFlags & ~G_InstanceFlagOwnedTransform;
    }

    if (Attached == Old)
        return GR_OK;

    Attached->Init(Instance);
    if (Old != nullptr)
    {
        CopyTransformState(Attached, Old, Instance);
        Old->Release(Instance);
    }
    if ((OldFlags & G_InstanceFlagOwnedTransform) != 0 && Old != nullptr)
        FreeInstanceTransform((FInstanceTransform*)Old); // flag 2: ours
    Instance->Transform = Attached;
    return GR_OK;
}

// LAB_066219b0 / IEngine +0x74 (RET 0xC): *Out = 0; instance != -1 -> *Out = inst+4 (only -1
// is tested). EAX = Out on return; the port returns 0.
static GENRESULT DACOM_API EngineGetInstanceTransformObject(FEngineObject* This,
                                                           FEngineInstance* Instance,
                                                           IInstanceTransform** Out)
{
    (void)This;
    *Out = nullptr;
    if (Instance != G_InvalidInstance)
        *Out = Instance->Transform;
    return GR_OK;
}

// 06621eb5..d1: IEC +0x28 UpdateInstance(inst, arg) on every node of +0x54, no tests.
static void FanoutIecUpdateInstance(FEngineObject* This, FEngineInstance* Instance, void* Arg)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->UpdateInstance(Instance, Arg);
}

static unsigned char EngineComposeWorld(FEngineInstance* Instance);

static void EngineUpdateInstanceTree(FEngineObject* This, FEngineInstance* Instance,
                                    unsigned long Flags, void* Arg)
{
    if ((Flags & 2) == 2)
        FanoutIecUpdateInstance(This, Instance, Arg);
    if ((Flags & 1) == 1)
        return;
    FEngineInstance* Child = Instance->FirstChild;
    while (Child != nullptr)
    {
        // FUN_06621ea0 always CALL FUN_0661f250 on each child; flag 8 is
        // only tested in FUN_06621e60 before FUN_06621f20 (ancestors).
        EngineComposeWorld(Child);
        EngineUpdateInstanceTree(This, Child, Flags, Arg);
        Child = Child->NextSibling;
    }
}

// The transform's vtable +0x34 with the instance; its AL decides the FUN_0661f210 recursion.
static unsigned char MarkInstanceWorldDirty(FEngineInstance* Instance) // formerly CallTransformSlot34
{
    return (unsigned char)Instance->Transform->MarkWorldDirty(Instance);
}

// FUN_0661f210 (cdecl): children from FirstChild (+0x1C) through NextSibling (+0x14); a child
// whose transform +0x34 answers AL != 0 is recursed into. No tests.
static void EngineWalkDirtyChildren(FEngineInstance* Instance)
{
    for (FEngineInstance* Child = Instance->FirstChild; Child != nullptr; Child = Child->NextSibling)
    {
        if (MarkInstanceWorldDirty(Child) != 0)
            EngineWalkDirtyChildren(Child);
    }
}

// DAT_0662b178: FUN_0661f250's own lazily created 3DMathEngine (0661f283..91).
static I3DMathEngine* G_ComposeMath = nullptr;

// FUN_0661f250 (thiscall on the instance), in retail order:
static unsigned char EngineComposeWorld(FEngineInstance* Instance)
{
    if (Instance->Parent == nullptr)
        return 0;
    const unsigned long bApplied = ApplyJointToLocal(Instance, 0);
    const float* ParentWorld = Instance->Parent->Transform->GetMatrix(Instance->Parent);
    if (G_ComposeMath == nullptr)
        G_ComposeMath = Create3DMathEngine();
    float Composed[12];
    G_ComposeMath->MulTransform(*(FTransform*)Composed, // +0x1C
                                        *(const FTransform*)ParentWorld,
                                        *(const FTransform*)(Instance->Matrix));
    float Copy[12];
    std::memcpy(Copy, Composed, sizeof(Copy));
    Instance->Transform->SetMatrix(Instance, Copy);
    if ((bApplied & 0xFF) != 0)
    {
        EngineInvalidateExtentChain(Instance, 1);
        if (MarkInstanceWorldDirty(Instance) != 0)
            EngineWalkDirtyChildren(Instance);
    }
    return 0;
}

// FUN_06621f20 (RET 4): instance non-null with a parent -> recurse on the parent, then
// FUN_0661f250 on the instance.
static void EngineComposeAncestors(FEngineInstance* Instance)
{
    if (Instance == nullptr || Instance->Parent == nullptr)
        return;
    EngineComposeAncestors(Instance->Parent);
    EngineComposeWorld(Instance);
}

// FUN_06621e60 / IEngine+0x6C RET 0x10: compose ancestors, IEC+0x28, walk children.
static GENRESULT DACOM_API EngineUpdateInstance(FEngineObject* This, FEngineInstance* Instance,
                                               unsigned long Flags, void* Arg)
{
    if (!IsValidInstance(Instance))
        return GR_OK;
    if ((Flags & 8) == 0)
        EngineComposeAncestors(Instance);
    EngineUpdateInstanceTree(This, Instance, Flags, Arg);
    return GR_OK;
}

// LAB_06621a40 / IEngine +0x7C (RET 0xC): instance -1 / 0 -> return; inst+0x28 = value. EAX
// is the instance argument on both paths; the port returns 0.
static GENRESULT DACOM_API EngineSetInstanceUserData(FEngineObject* This, FEngineInstance* Instance,
                                                    void* Value)
{
    (void)This;
    if (Instance != G_InvalidInstance && Instance != nullptr)
        Instance->UserData = Value;
    return GR_OK;
}

// LAB_06621a60 / IEngine +0x80 (RET 8): instance -1 / 0 -> 0; else inst+0x28.
static void* DACOM_API EngineGetInstanceUserData(FEngineObject* This, FEngineInstance* Instance)
{
    (void)This;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return nullptr;
    return Instance->UserData;
}

// LAB_06621a80 / IEngine +0x84 (RET 0x14): instance -1 / 0 -> return; otherwise
// FUN_0661ece0(inst, Extent, Vec, Flags & 1). EAX is left from the callee (AL = 1 over the
// instance pointer); the port returns 0.
static GENRESULT DACOM_API EngineSetInstanceExtent(FEngineObject* This, FEngineInstance* Instance,
                                                  unsigned long Flags, float Extent, const float* Vec)
{
    (void)This;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return GR_OK;
    EngineSetExtentAndInvalidate(Instance, Extent, Vec, (unsigned char)(Flags & 1));
    return GR_OK;
}

// Distance on the FPU as FUN_0661ed90 does it (0661f0ac..d4 and 0661f14c..74): the three
// differences stay unrounded and the squares are summed z, y, x.
static double ExtentDistance(const float* A, const float* B)
{
    const double Dx = (double)(A[0]) - (double)(B[0]);
    const double Dy = (double)(A[1]) - (double)(B[1]);
    const double Dz = (double)(A[2]) - (double)(B[2]);
    return std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx);
}

// FUN_0661ed90, thiscall (Instance) RET 0xC (bRecursive, OutExtent, OutVec), 0661ed90..f209.
static void EngineComputeInstanceExtent(FEngineInstance* Instance, unsigned char bRecursive,
                                        float* OutExtent, float* OutVec)
{
    if (bRecursive == 0)
    {
        InstanceTransformExtra(Instance, OutExtent, OutVec);
        return;
    }
    if (Instance->Extent > 0.0f)
    {
        *OutExtent = Instance->Extent;
        OutVec[0] = Instance->ExtentVec[0];
        OutVec[1] = Instance->ExtentVec[1];
        OutVec[2] = Instance->ExtentVec[2];
        return;
    }

    float* WorldXf = Instance->Transform->GetMatrix(Instance);
    float OwnR = 0.0f;
    float OwnLocal[3];
    InstanceTransformExtra(Instance, &OwnR, OwnLocal);
    float OwnWorld[3];
    Acquire3DMathEngine()->TransformXform(*(FVector*)OwnWorld,
                                                  *(const FTransform*)WorldXf,
                                                  *(const FVector*)OwnLocal);

    float MinV[3];
    float MaxV[3];
    for (unsigned long Axis = 0; Axis < 3; ++Axis)
    {
        MinV[Axis] = OwnWorld[Axis] - OwnR;
        MaxV[Axis] = OwnWorld[Axis] + OwnR;
    }

    FEngineInstance* Child = Instance->FirstChild;
    while (Child != nullptr)
    {
        float* ChildXf = Child->Transform->GetMatrix(Child);
        float ChildR = 0.0f;
        float ChildLocal[3];
        EngineComputeInstanceExtent(Child, 1, &ChildR, ChildLocal);
        float ChildWorld[3];
        Acquire3DMathEngine()->TransformXform(*(FVector*)ChildWorld,
                                                      *(const FTransform*)ChildXf,
                                                      *(const FVector*)ChildLocal);
        for (unsigned long Axis = 0; Axis < 3; ++Axis)
        {
            const float Lo = ChildWorld[Axis] - ChildR;
            if (Lo < MinV[Axis])
                MinV[Axis] = Lo;
            const float Hi = ChildWorld[Axis] + ChildR;
            if (MaxV[Axis] < Hi)
                MaxV[Axis] = Hi;
        }
        Child = Child->NextSibling;
    }

    float CenterW[3];
    CenterW[0] = (MaxV[0] + MinV[0]) * 0.5f;
    CenterW[1] = (MaxV[1] + MinV[1]) * 0.5f;
    CenterW[2] = (MaxV[2] + MinV[2]) * 0.5f;
    const float SpanZ = MaxV[2] - MinV[2];
    const float SpanY = MaxV[1] - MinV[1];
    const float SpanX = MaxV[0] - MinV[0];
    float Widest = SpanX < SpanY ? SpanY : SpanX;
    if (Widest < SpanZ)
        Widest = SpanZ;
    float Radius = Widest * 0.5f;
    const double OwnSphere = ExtentDistance(CenterW, OwnWorld) + (double)OwnR;
    if (OwnSphere > (double)Radius)
        Radius = (float)OwnSphere;

    Child = Instance->FirstChild;
    while (Child != nullptr)
    {
        float* ChildXf = Child->Transform->GetMatrix(Child);
        float ChildR = 0.0f;
        float ChildLocal[3];
        EngineComputeInstanceExtent(Child, 1, &ChildR, ChildLocal);
        float ChildWorld[3];
        Acquire3DMathEngine()->TransformXform(*(FVector*)ChildWorld,
                                                      *(const FTransform*)ChildXf,
                                                      *(const FVector*)ChildLocal);
        const float ChildSphere =
            (float)(ExtentDistance(ChildWorld, CenterW) + (double)ChildR);
        if (ChildSphere > Radius)
            Radius = ChildSphere;
        Child = Child->NextSibling;
    }

    float LocalCenter[3];
    Acquire3DMathEngine()->InverseTransform(*(FVector*)LocalCenter,
                                                    *(const FTransform*)WorldXf,
                                                    *(const FVector*)CenterW);
    Instance->Extent = Radius;
    Instance->ExtentVec[0] = LocalCenter[0];
    Instance->ExtentVec[1] = LocalCenter[1];
    Instance->ExtentVec[2] = LocalCenter[2];
    *OutExtent = Radius;
    OutVec[0] = LocalCenter[0];
    OutVec[1] = LocalCenter[1];
    OutVec[2] = LocalCenter[2];
}

// LAB_06621ab0, IEngine slot 34 (+0x88), RET 0x14 (Instance, Flags, OutExtent, OutVec),
static GENRESULT DACOM_API EngineGetInstanceExtent(FEngineObject* This, FEngineInstance* Instance,
                                                  unsigned long Flags, float* OutExtent,
                                                  float* OutVec)
{
    (void)This;
    if (!IsValidInstance(Instance))
    {
        *OutExtent = -1.0f;
        OutVec[2] = 0.0f;
        OutVec[1] = 0.0f;
        OutVec[0] = 0.0f;
        return GR_OK;
    }
    if ((Flags & 1) != 0)
    {
        EngineComputeInstanceExtent(Instance, 0, OutExtent, OutVec);
        return GR_OK;
    }
    EngineComputeInstanceExtent(Instance, 1, OutExtent, OutVec);
    return GR_OK;
}

// The seven IEngine wrappers below used to go through two helpers that took the slot as a
static GENRESULT DACOM_API EngineSetInstancePosition(FEngineObject* This, FEngineInstance* Instance,
                                                    void* Position)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetPosition(Instance, (const float*)Position);
}

// LAB_06621b40 / IEngine +0x90 (RET 8) -> transform +0x10; fallback [DAT_0662b124].
static float* DACOM_API EngineGetInstancePosition(FEngineObject* This, FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix + 9;
    return Instance->Transform->GetPosition(Instance);
}

// LAB_06621b70 / IEngine +0x94 (RET 0xC) -> transform +0x14.
static GENRESULT DACOM_API EngineSetInstanceOrientation(FEngineObject* This,
                                                       FEngineInstance* Instance, void* Rows)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetOrientation(Instance, (const float*)Rows);
}

// LAB_06621ba0 / IEngine +0x98 (RET 8) -> transform +0x18; fallback [DAT_0662b110].
static float* DACOM_API EngineGetInstanceOrientation(FEngineObject* This, FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix;
    return Instance->Transform->GetOrientation(Instance);
}

// LAB_06621bd0 / IEngine +0x9C (RET 0xC) -> transform +0x1C.
static GENRESULT DACOM_API EngineSetInstanceMatrix(FEngineObject* This, FEngineInstance* Instance,
                                                  void* Matrix)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetMatrix(Instance, (const float*)Matrix);
}

// LAB_06621c00 / IEngine +0xA0 (RET 8) -> transform +0x20; fallback DAT_0662b128 itself.
static float* DACOM_API EngineGetInstanceTransform(FEngineObject* This, FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix;
    return Instance->Transform->GetMatrix(Instance);
}

// LAB_06621c30 / IEngine +0xA8 and +0xB0 (RET 8): return [DAT_0662b124] -- the zero
// translation of the identity, not the matrix.
static float* DACOM_API EngineGetIdentityMatrix(FEngineObject* This, void* Unused)
{
    (void)This;
    (void)Unused;
    return G_IdentityMatrix + 9;
}

// LAB_06621c40 / IEngine +0xB4 (RET 0xC) -> transform +0x2C.
static GENRESULT DACOM_API EngineSetTransformUser(FEngineObject* This, FEngineInstance* Instance,
                                                 void* User)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetUser(Instance, (unsigned long)User);
}

// LAB_06621c70 / IEngine +0xB8 (RET 8) -> transform +0x30; fallback 0.
static unsigned long DACOM_API EngineGetTransformUser(FEngineObject* This, FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return 0;
    return Instance->Transform->GetUser(Instance);
}

// LAB_06622580 / IEngine+0xC4 RET 8: parent ? inst+0x5C : 0.
static unsigned long* DACOM_API EngineGetInstanceRelative(FEngineObject* This,
                                                         FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr)
        return nullptr;
    return Instance->Relative;
}

static void CopyJointFloats(float* Dest, const float* Source, unsigned long Count)
{
    if (Dest == nullptr || Source == nullptr)
        return;
    unsigned long Index = 0;
    while (Index < Count)
    {
        Dest[Index] = Source[Index];
        Index += 1;
    }
}

// LAB_066226e0 / IEngine+0xC8 RET 0x10: set stored joint state. No solver.
static unsigned long DACOM_API EngineSetJointParams(FEngineObject* This, FEngineInstance* Instance,
                                                   unsigned long Index, const float* Values)
{
    (void)This;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr || Values == nullptr)
        return 0;
    if (Index != 0)
    {
        // LAB_066226e0 Index!=0 → caseD_6 copy 7 floats; +0xD4 only on Index==0 path.
        CopyJointFloats(Instance->JointState, Values, 7);
        return 1;
    }

    Instance->bWorldDirty = 1;
    const unsigned long Type = Instance->Relative[0];
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
    {
        const float Input = Values[0];
        const float MinLimit = *(const float*)(&Instance->Relative[0x13]);
        const float MaxLimit = *(const float*)(&Instance->Relative[0x14]);
        if (MinLimit > Input)
            Instance->JointState[0] = MinLimit;
        else if (MaxLimit < Input)
            Instance->JointState[0] = MaxLimit;
        else
            Instance->JointState[0] = Input;
        return 1;
    }
    if (Type == G_JointTypeCyl)
    {
        CopyJointFloats(Instance->JointState, Values, 2);
        return 1;
    }
    if (Type == G_JointTypeSphere)
    {
        CopyJointFloats(Instance->JointState + 3, Values, 4);
        return 1;
    }
    if (Type == G_JointTypeTrans)
    {
        CopyJointFloats(Instance->JointState, Values, 3);
        return 1;
    }
    if (Type == G_JointTypeLoose)
    {
        CopyJointFloats(Instance->JointState, Values, 7);
        return 1;
    }
    return 1;
}

// LAB_066225b0 / IEngine+0xCC RET 0x10: get stored joint state. No solver.
static unsigned long DACOM_API EngineGetJointParams(FEngineObject* This, FEngineInstance* Instance,
                                                   unsigned long Index, float* Out)
{
    (void)This;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr || Out == nullptr)
        return 0;
    if (Index != 0)
    {
        CopyJointFloats(Out, Instance->JointState, 7);
        return 1;
    }

    const unsigned long Type = Instance->Relative[0];
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
    {
        CopyJointFloats(Out, Instance->JointState, 1);
        return 1;
    }
    if (Type == G_JointTypeCyl)
    {
        CopyJointFloats(Out, Instance->JointState, 2);
        return 1;
    }
    if (Type == G_JointTypeSphere)
    {
        CopyJointFloats(Out, Instance->JointState + 3, 4);
        return 1;
    }
    if (Type == G_JointTypeTrans)
    {
        CopyJointFloats(Out, Instance->JointState, 3);
        return 1;
    }
    if (Type == G_JointTypeLoose)
    {
        CopyJointFloats(Out, Instance->JointState, 7);
        return 1;
    }
    return 1;
}

// LAB_066224b0 / IEngine+0xD4 RET 8: inst+0x10 or -1.
static FEngineInstance* DACOM_API EngineGetInstanceParent(FEngineObject* This,
                                                         FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr)
        return G_InvalidInstance;
    return Instance->Parent;
}

// LAB_066224d0 / IEngine+0xD8 RET 0x10: first child / next sibling / parent-walk.
static FEngineInstance* DACOM_API EngineGetRelatedInstance(FEngineObject* This,
                                                          FEngineInstance* From,
                                                          unsigned long Flags,
                                                          FEngineInstance* Cursor)
{
    (void)This;
    FEngineInstance* Root = IsValidInstance(From) ? From : nullptr;
    FEngineInstance* Node = IsValidInstance(Cursor) ? Cursor : nullptr;
    if (Root == nullptr)
        return G_InvalidInstance;
    if (Node == nullptr)
        return Root->FirstChild != nullptr ? Root->FirstChild : G_InvalidInstance;
    if ((Flags & 1) != 0)
        return Node->NextSibling != nullptr ? Node->NextSibling : G_InvalidInstance;
    if (Node->FirstChild != nullptr)
        return Node->FirstChild;
    while (Node != nullptr)
    {
        if (Node->NextSibling != nullptr)
            return Node->NextSibling;
        Node = Node->Parent;
        if (Node == Root)
            break;
    }
    return G_InvalidInstance;
}

// LAB_06622480 / IEngine+0xDC RET 8: walk +0x10 to root.
static FEngineInstance* DACOM_API EngineGetInstanceRoot(FEngineObject* This,
                                                       FEngineInstance* Instance)
{
    (void)This;
    if (!IsValidInstance(Instance))
        return G_InvalidInstance;
    while (Instance->Parent != nullptr)
        Instance = Instance->Parent;
    return Instance;
}

// LAB_06622450 / IEngine+0xE0 RET 0xC: find archetype child record by nested handle.
static FArchetypeChild* DACOM_API EngineFindArchetypeChild(FEngineObject* This,
                                                          FArchetypeHandle* Handle,
                                                          FArchetypeHandle* ChildHandle)
{
    (void)This;
    if (Handle == G_InvalidArchetype || Handle == nullptr)    // 06622454 / 06622459
        return nullptr;
    FArchetypeChild* Child = Handle->Children.data();          // 0662245d..60: end read once
    FArchetypeChild* const End = Child + Handle->Children.size();
    while (Child != End)
    {
        if (Child->Handle == ChildHandle)                      // 0662246b
            return Child;
        Child += 1;
    }
    return nullptr;
}

// LAB_06612df0 / IEngine+0xE4 RET 0xC: always −1. Not get/set.
// formerly EngineSlot57ReturnNeg1
static unsigned long DACOM_API RejectEngineOperation(FEngineObject* This, void* A, void* B)
{
    (void)This;
    (void)A;
    (void)B;
    return 0xFFFFFFFF;
}

// LAB_066222a0 / IEngine+0xD0 (stdcall RET 8): iterate the instance-root map. No checks.
static FEngineInstance* DACOM_API EngineFindMappedInstance(FEngineObject* This,
                                                          FEngineInstance* Instance)
{
    if (Instance == G_InvalidInstance)
    {
        if (This->Instances.empty())
            return G_InvalidInstance;
        return (FEngineInstance*)(
            (intptr_t)(This->Instances.begin()->first));
    }
    const long Key = EngineInstanceKey(Instance);
    This->InstanceLookupCount += 1;
    FEngineInstanceMap::iterator It;
    if (This->bInstanceCacheValid != 0 && This->CachedInstance->first == Key)
    {
        This->InstanceCacheHits += 1;
        It = This->CachedInstance;
    }
    else
    {
        It = This->Instances.find(Key);
        if (It != This->Instances.end())
        {
            This->CachedInstance = It;
            This->bInstanceCacheValid = 1;
        }
    }
    if (It == This->Instances.end())
        return G_InvalidInstance;
    ++It;
    if (It == This->Instances.end())
        return G_InvalidInstance;
    return (FEngineInstance*)((intptr_t)(It->first));
}

static I3DMathEngine* G_MathEngine = nullptr;

// FUN_06628190: descriptor {0xC, "3DMathEngine", 0} on the stack, DACOM_Acquire() +0xC
// CreateInstance into a local, return the local (hr not tested; the local is uninitialised in
// retail, 0 here). No other tests.
I3DMathEngine* Create3DMathEngine()
{
    FAggDescC Desc = {};
    Desc.Size = G_AggDescSizeMath;
    Desc.Name = G_Name3DMathEngine;
    void* Created = nullptr;
    DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc), &Created);
    return (I3DMathEngine*)Created;
}

// Shared with EngBaseMixer.cpp (declared in EngBase.h). Retail keeps one lazily-filled global
// per call site (DAT_0662acf4, DAT_0662b1b0, ...) with the same `if (!g) g = FUN_06628190()`
// shape; the sites that still share this one cache are noted on the decomp-tracker EngBase items.
I3DMathEngine* Acquire3DMathEngine()
{
    if (G_MathEngine == nullptr)
        G_MathEngine = Create3DMathEngine();
    return G_MathEngine;
}

// LAB_066212c0 / IEngine+0x34 RET 0xC.
static FArchetypeHandle* DACOM_API EngineDuplicateArchetype(FEngineObject* This,
                                                           FArchetypeHandle* ArchIn,
                                                           FArchetypeName* Name)
{
    if (ArchIn == G_InvalidArchetype)
        return G_InvalidArchetype;
    if (ArchIn != nullptr && ArchIn->Children.size() != 0) // 066212d6..fb
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x42A, "WARNING",
                     "Engine: duplicate_archetype: arch_index is a compound archetype (not valid).");
        return G_InvalidArchetype;
    }
    if (LookupArchetype(This, Name) != G_InvalidArchetype)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x42F, "WARNING",
                     "Engine: duplicate_archetype: archetype name already exists in duplicate_archetype().");
        return G_InvalidArchetype;
    }

    FArchetypeHandle* NewHandle =
        (FArchetypeHandle*)(This->CreateCompoundArchetype(Name)); // vtable +0x2C
    if (NewHandle == G_InvalidArchetype)
        return G_InvalidArchetype;
    FanoutIecDuplicateArchetype(This, NewHandle, ArchIn);
    return NewHandle;
}

// FUN_06621560 / IEngine+0x48 RET 0x10: IEC+0x1C (id, name, out); first HRESULT>=0 wins.
static GENRESULT DACOM_API EngineQueryEngineComponent(FEngineObject* This, int Id, void* Name,
                                                     void** Out)
{
    // 06621560: *Out = 0 (no test); Id -1 or Name null -> -2; IEC +0x1C (Id, Name, Out) over
    // +0x54, the first hr >= 0 -> 0; none -> -1. No other checks.
    *Out = nullptr;
    if (Id == -1 || Name == nullptr)
        return GR_INVALID_PARMS;

    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        const GENRESULT Result = AsEngineComponent(Node->Object)->QueryArchetype(
            (void*)((ptrdiff_t)Id), (const char*)Name, Out);
        if (Result >= 0)
            return GR_OK;
    }
    return (GENRESULT)(-1);
}

// The map key: retail stores the archetype handle as a signed int (FUN_06612e70, JGE).
static long HardpointKeyOf(FArchetypeHandle* Handle)
{
    return (long)((intptr_t)Handle);
}

// The lookup every IHardpoint / IEngineComponent method of this object opens with (inline in
static FHardpointMap::iterator HardpointLookup(FHardpointObject* This, FArchetypeHandle* Handle)
{
    const long Key = HardpointKeyOf(Handle);
    This->LookupCount += 1;
    if (This->bCacheValid != 0 && This->CachedNode->first == Key)
    {
        This->CacheHits += 1;
        return This->CachedNode;
    }
    FHardpointMap::iterator Found = This->Archetypes.find(Key);
    if (Found != This->Archetypes.end())
    {
        This->CachedNode = Found;
        This->bCacheValid = 1;
    }
    return Found;
}

// FUN_06626350 / IHardpoint+0x0C (stdcall RET 0x10). No argument checks.
static unsigned long DACOM_API HardpointGetHardpoint(FHardpointObject* This,
                                                    FArchetypeHandle* Handle,
                                                    FHardpointKey const* Key, void* Out)
{
    FHardpointMap::iterator Entry = HardpointLookup(This, Handle);
    if (Entry == This->Archetypes.end())
        return 0;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
    {
        if (It->Crc == Key->Crc)
        {
            std::memcpy(Out, It->Data, sizeof(It->Data));
            return 1;
        }
    }
    return 0;
}

// LAB_066263e0 / IHardpoint+0x10 (stdcall RET 0x10). No argument checks.
static unsigned long DACOM_API HardpointAddHardpoint(FHardpointObject* This,
                                                    FArchetypeHandle* Handle,
                                                    FHardpointKey const* Key, const void* Data)
{
    FHardpointMap::iterator Entry = HardpointLookup(This, Handle);
    if (Entry == This->Archetypes.end())
    {
        Entry = This->Archetypes
                    .insert(FHardpointMap::value_type(HardpointKeyOf(Handle), FHardpointList()))
                    .first;
        if (Entry != This->Archetypes.end())
        {
            This->CachedNode = Entry;
            This->bCacheValid = 1;
        }
        if (Entry == This->Archetypes.end())
            return 0;
    }
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
    {
        if (It->Crc == Key->Crc)
        {
            std::memcpy(It->Data, Data, sizeof(It->Data));
            return 1;
        }
    }
    FHardpointRecord Temp(Key->Crc, Key->Name);
    std::memcpy(Temp.Data, Data, sizeof(Temp.Data));
    List.push_back(Temp);
    return 1;
}

// FUN_06626610 / IHardpoint+0x14 (stdcall RET 0x10). No argument checks.
static GENRESULT DACOM_API HardpointEnumerateHardpoints(FHardpointObject* This,
                                                       FHardpointEnumProc Callback,
                                                       FArchetypeHandle* Handle, void* Context)
{
    FHardpointMap::iterator Entry = HardpointLookup(This, Handle);
    if (Entry == This->Archetypes.end())
        return GR_OK;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
        Callback(It->Name, Context);
    return GR_OK;
}

// FUN_066269b0 / IHardpoint+0x1C (stdcall RET 0xC). No argument checks.
static char const* DACOM_API HardpointFindHardpoint(FHardpointObject* This,
                                                   FArchetypeHandle* Handle,
                                                   unsigned long const* Crc)
{
    FHardpointMap::iterator Entry = HardpointLookup(This, Handle);
    if (Entry == This->Archetypes.end())
        return nullptr;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
    {
        if (It->Crc == *Crc)
            return It->Name;
    }
    return nullptr;
}

// LAB_06626690 / IHardpoint+0x18 (stdcall RET 0x14): connect InstA's hardpoint KeyA to InstB's
static unsigned long DACOM_API HardpointConnect(FHardpointObject* This, FEngineInstance* InstA,
                                               FHardpointKey const* NameA, FEngineInstance* InstB,
                                               FHardpointKey const* NameB)
{
    IEngine* Engine = This->Engine;
    void* HandleB = G_InvalidArchetype;
    unsigned long Status = 1;
    unsigned char DataA[0x48];
    unsigned char DataB[0x48];

    void* HandleA = Engine->GetInstanceArchetype(InstA);
    if (HandleA != G_InvalidArchetype && This->GetHardpoint(HandleA, NameA, DataA) != 0)
    {
        Status = 2;
        HandleB = Engine->GetInstanceArchetype(InstB);
        if (HandleB != G_InvalidArchetype && This->GetHardpoint(HandleB, NameB, DataB) != 0)
        {
            unsigned long Payload[G_EngineJointPayloadDwords];
            std::memset(Payload, 0, sizeof(Payload));
            Status = 3;
            const unsigned long TypeB = *(unsigned long*)DataB;
            Payload[0] = TypeB;
            float* PayloadF = (float*)Payload;
            const float* PosA = (const float*)(DataA + 4);
            const float* PosB = (const float*)(DataB + 4);
            const float* OrientA = (const float*)(DataA + 0x10);
            const float* OrientB = (const float*)(DataB + 0x10);
            const float* AxisB = (const float*)(DataB + 0x34);

            if (TypeB == 0)
            {
                const float Transposed[9] = {OrientB[0], OrientB[3], OrientB[6],
                                             OrientB[1], OrientB[4], OrientB[7],
                                             OrientB[2], OrientB[5], OrientB[8]};
                float OutMat[9];
                float OutVec[3];
                MathMulMatrix3(Acquire3DMathEngine(), OutMat, OrientA, Transposed);
                MathTransformVec(Acquire3DMathEngine(), OutVec, OutMat, PosB);
                PayloadF[4] = PosA[0] - OutVec[0];
                PayloadF[5] = PosA[1] - OutVec[1];
                PayloadF[6] = PosA[2] - OutVec[2];
                std::memcpy(PayloadF + 7, OutMat, sizeof(OutMat));
            }
            else if (TypeB == 1 || TypeB == 2)
            {
                std::memcpy(PayloadF + 1, PosA, 3 * sizeof(float));
                std::memcpy(PayloadF + 4, PosB, 3 * sizeof(float));
                float OutMat[9];
                float AxisXformed[3];
                MathMulMatrix3(Acquire3DMathEngine(), OutMat, OrientA, OrientB);
                MathTransformVec(Acquire3DMathEngine(), AxisXformed, OutMat, AxisB);
                std::memcpy(PayloadF + 7, OutMat, sizeof(OutMat));
                std::memcpy(PayloadF + 16, AxisXformed, sizeof(AxisXformed));
                std::memcpy(PayloadF + 19, DataB + 0x40, 2 * sizeof(float));
            }

            if (Engine->CreateJoint(InstA, InstB, Payload) != 0)
                Status = 0;
        }
    }

    Engine->ReleaseHandle(HandleB);
    Engine->ReleaseHandle(HandleA);
    return Status;
}

// LAB_06621f90 / IEngine+0xBC (stdcall RET 0x10), listing 34640-34796:
static unsigned long DACOM_API EngineCreateJoint(FEngineObject* This, FEngineInstance* Parent,
                                                FEngineInstance* Child, const unsigned long* Payload)
{
    if (Parent == G_InvalidInstance || Parent == nullptr)
        return 0;
    if (Child == G_InvalidInstance || Child == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x741, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: create_joint: Failing to connect %d <- %d, "
                                        "invalid instances.",
                                        Parent, Child));
        return 0;
    }
    if (Child->Parent != nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x746, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: create_joint: Failing to connect %d <- %d, "
                                        "child already has a parent.\n",
                                        Parent, Child));
        return 0;
    }
    if ((long)(Child->ChildCount) > 0)
        EraseRootInstance(This, Child);
    ConnectInstanceChild(Parent, Child, Payload);
    if (Parent->ChildCount == 1 && Parent->Parent == nullptr)
        InsertRootInstance(This, Parent);
    This->UpdateInstance(Child, 0, nullptr);
    return 1;
}

// LAB_06622140 / IEngine+0xC0 (stdcall RET 0xC), listing 34798-34922. The instance acted on is
static unsigned long DACOM_API EngineDestroyJoint(FEngineObject* This, FEngineInstance* Parent,
                                                 FEngineInstance* Child)
{
    if (Child == G_InvalidInstance || Child == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x763, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: destroy_joint: Failing to disconnect %d <- %d, "
                                        "child not found.",
                                        Parent, Child));
        return 0;
    }
    if (Child->Parent == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x769, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: destroy_joint: Failing to disconnect %d <- %d, "
                                        "not connected.",
                                        Parent, Child));
        return 0;
    }
    FEngineInstance* OldParent = Child->Parent;
    UnlinkInstanceChild(OldParent, Child);
    if (OldParent->ChildCount == 0 && OldParent->Parent == nullptr)
        EraseRootInstance(This, OldParent);
    if ((long)(Child->ChildCount) > 0)
        InsertRootInstance(This, Child);
    return 1;
}

// LAB_06625e00 / IEC+0xC (stdcall RET 4). QI through the IEC facet (-> 06628130 -> the
static GENRESULT DACOM_API HardpointEngineComponentInitialize(void* Nested)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    if (HardpointQueryInterface(Object, Iids::Engine, (void**)(&Object->Engine)) < 0)
    {
        EngBaseFdump(kFdumpError, G_FdumpFileHardPoint, 0xDC, "ERROR",
                     "HardPoint: Initialize: unable to acquire IID_IEngine, cannot continue");
        return GR_GENERIC;
    }
    Object->Engine->Release();
    return GR_OK;
}

// FUN_06626d00 (thiscall, ECX unused, RET 4). Case-sensitive compares, in this order:
// "Fixed" -> 0, "Revolute" -> 2, "Prismatic" -> 1; anything else FDUMPs 0x100002
// HardPoint.cpp 0x25F and returns 0 (JT_FIXED, as the message says). No null check.
static long ParseHardpointJointType(const char* Name)
{
    if (std::strcmp(Name, G_NameFixed) == 0)
        return 0;
    if (std::strcmp(Name, G_NameRevolute) == 0)
        return 2;
    if (std::strcmp(Name, G_NamePrismatic) == 0)
        return 1;
    EngBaseFdump(kFdumpWarning, G_FdumpFileHardPoint, 0x25F, "WARNING",
                 "HardPoint: unknown joint type, returning JT_FIXED");
    return 0;
}

// The fixed-size field read LAB_06625e60 inlines four times (Axis 066260b8..1e, Min
static void ReadHardpointField(IFileSystem* Fs, const char* Name, void* Dest, unsigned long Size)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int FileHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (FileHandle == -1)
        return;
    unsigned char Temp[0xC];
    unsigned long BytesRead = 0;
    if (Fs->Read((unsigned long)FileHandle, Temp, Size, &BytesRead, 0) != 0 && // +0x14
        BytesRead == Size)
        std::memcpy(Dest, Temp, Size);
    Fs->CloseFile((unsigned long)FileHandle); // +0x10
}

// FUN_06627b30 (cdecl, FS, Name, Out): the same open as ReadHardpointField, a 0x24-byte Read
static GENRESULT ReadHardpointOrientation(IFileSystem* Fs, const char* Name, float* Out)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int FileHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    bool bRead = false;
    if (FileHandle != -1)
    {
        float Temp[9];
        unsigned long BytesRead = 0;
        if (Fs->Read((unsigned long)FileHandle, Temp, sizeof(Temp), &BytesRead, 0) !=
                0 &&
            BytesRead == sizeof(Temp))
        {
            bRead = true;
            std::memcpy(Out, Temp, sizeof(Temp));
        }
        Fs->CloseFile((unsigned long)FileHandle); // +0x10
    }
    return bRead ? GR_OK : GR_E_FAIL;
}

// LAB_06625e60 / IEC+0x10 (stdcall RET 0xC, this = obj+4). No argument checks.
static GENRESULT DACOM_API HardpointCreateArchetype(void* Nested, FArchetypeHandle* Handle,
                                                   void* FileSystem)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    IFileSystem* Fs = AsFileSystem(FileSystem);
    FHardpointMap::iterator Entry = Object->Archetypes.end();

    if (Fs->OpenNode(G_NameHardpointsFolder) == 0) // +0x60
        return 0;

    WIN32_FIND_DATAA Outer;
    const int OuterFind = Fs->FindFirst(G_NameStar, // +0x48
                                     &Outer);
    if (OuterFind == -1)
        return 0;

    do
    {
        if (Fs->OpenNode(Outer.cFileName) == 0)
        {
            char Message[0x2000];
            EngBaseFdump(kFdumpWarning, G_FdumpFileHardPoint, 0xFF, "WARNING",
                         FormatFdumpMessage(Message,
                                            "Hardpoint: create_archetype: '%s' has old hardpoint "
                                            "layout. If you see this message, mail pbleisch\n",
                                            Outer.cFileName));
            continue;
        }

        WIN32_FIND_DATAA Inner;
        const int InnerFind = Fs->FindFirst("*.*", // +0x48
                                         &Inner);
        if (InnerFind != -1)
        {
            do
            {
                if (Fs->OpenNode(Inner.cFileName) == 0)
                    continue;
                if (Entry == Object->Archetypes.end())
                {
                    Entry = Object->Archetypes
                                .insert(FHardpointMap::value_type(HardpointKeyOf(Handle),
                                                                  FHardpointList()))
                                .first;
                    if (Entry != Object->Archetypes.end())
                    {
                        Object->CachedNode = Entry;
                        Object->bCacheValid = 1;
                    }
                }
                {
                    FHardpointRecord Temp(DACOM_CRC::GetCRC32(Inner.cFileName), Inner.cFileName);
                    Entry->second.push_back(Temp);
                }
                unsigned char* Data = Entry->second.back().Data;
                const long Type = ParseHardpointJointType(Outer.cFileName);
                std::memcpy(Data, &Type, sizeof(Type));
                // 066260a3 JZ (0 -> Position), 066260a9 JLE (< 0 -> FDUMP), 066260b2 JG (> 2 ->
                // FDUMP): signed compares against 0 and 2.
                if (Type < 0 || Type > 2)
                {
                    EngBaseFdump(kFdumpWarning, G_FdumpFileHardPoint, 0x120, "WARNING",
                                 "Hardpoint: unknown hardpoint type, probably random garbage");
                }
                else
                {
                    if (Type != 0)
                    {
                        ReadHardpointField(Fs, G_NameAxis, Data + 0x34, 0xC);
                        ReadHardpointField(Fs, G_NameMin, Data + 0x40, 4);
                        ReadHardpointField(Fs, G_NameMax, Data + 0x44, 4);
                    }
                    ReadHardpointField(Fs, G_NamePosition, Data + 4, 0xC);
                    ReadHardpointOrientation(Fs, G_NameOrientation,
                                             (float*)(Data + 0x10));
                }
                Fs->OpenNode(G_NameParentDot); // +0x60
            } while (Fs->FindNext((unsigned long)InnerFind, // +0x4C
                                &Inner) != 0);
            Fs->FindClose((unsigned long)InnerFind); // +0x50
        }
        Fs->OpenNode(G_NameParentDot); // +0x60
    } while (Fs->FindNext((unsigned long)OuterFind, // +0x4C
                        &Outer) != 0);
    Fs->FindClose((unsigned long)OuterFind); // +0x50
    Fs->OpenNode(G_NameParentDot);                     // +0x60
    return Entry != Object->Archetypes.end() ? 1 : 0;
}

// LAB_06626b90 / IEC+0x14 (stdcall RET 0xC, this = obj+4; NewHandle = arg 2, OldHandle =
static GENRESULT DACOM_API HardpointDuplicateArchetype(void* Nested, FArchetypeHandle* NewHandle,
                                                      FArchetypeHandle* OldHandle)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    FHardpointMap::iterator Source = HardpointLookup(Object, OldHandle);
    if (Source == Object->Archetypes.end())
        return GR_OK;
    FHardpointList Copy;
    for (FHardpointList::iterator It = Source->second.begin(); It != Source->second.end(); ++It)
        Copy.push_back(*It);
    FHardpointMap::iterator Inserted =
        Object->Archetypes.insert(FHardpointMap::value_type(HardpointKeyOf(NewHandle), Copy)).first;
    if (Inserted != Object->Archetypes.end())
    {
        Object->CachedNode = Inserted;
        Object->bCacheValid = 1;
    }
    return GR_OK;
}

// LAB_06626a30 / IEC+0x18 (stdcall RET 8, this = obj+4). No argument checks.
static GENRESULT DACOM_API HardpointDestroyArchetype(void* Nested, FArchetypeHandle* Handle)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    if (HardpointLookup(Object, Handle) == Object->Archetypes.end())
        return GR_OK;
    const long Key = HardpointKeyOf(Handle);
    if (Object->bCacheValid != 0 && Object->CachedNode->first == Key)
        Object->bCacheValid = 0;
    Object->Archetypes.erase(Key);
    return GR_OK;
}

// Facet wrappers retain their specific interface receiver.
unsigned long DACOM_API FAnimationChannelFacet::CreateTyped(void* Owner, unsigned long Type,
                                                            void* BindA, void* BindB)
{
    return ChannelCreateTyped(this, Owner, Type, BindA, BindB);              // LAB_06615950
}
unsigned long DACOM_API FAnimationChannelFacet::CreateFromObject(void* Object, void* BindA,
                                                                 void* BindB)
{
    return ChannelCreateFromObject(this, Object, BindA, BindB);              // LAB_06615b00
}
unsigned long DACOM_API FAnimationChannelFacet::Destroy(unsigned long Key)
{
    return ChannelDestroy(this, Key);                                        // FUN_06615e10
}
unsigned long DACOM_API FAnimationChannelFacet::GetObject(unsigned long Key, void** Out)
{
    return ChannelGetObject(this, Key, Out);                                 // LAB_06615d80
}
unsigned long DACOM_API FAnimationChannelFacet::Play(unsigned long Key, float* Time)
{
    return ChannelPlay(this, Key, Time);
}
unsigned long DACOM_API FAnimationChannelFacet::StopGraph(unsigned long Key)
{
    return ChannelStopGraph(this, Key);
}
float DACOM_API FAnimationChannelFacet::GetDuration(unsigned long Key)
{
    return ChannelGetDuration(this, Key);
}
unsigned long DACOM_API FAnimationChannelFacet::Blend(unsigned long Key, float Duration)
{
    return ChannelBlend(this, Key, Duration);
}
GENRESULT DACOM_API FAnimationChannelFacet::SetUser(unsigned long Key, unsigned long User)
{
    return ChannelSetUser(this, Key, User);
}
float DACOM_API FAnimationChannelFacet::GetUser(unsigned long Key)
{
    return ChannelGetUser(this, Key);
}
GENRESULT DACOM_API FAnimationChannelFacet::SetOverlayPos(unsigned long Key, float* Pos)
{
    return ChannelSetOverlayPos(this, Key, Pos);
}
GENRESULT DACOM_API FAnimationChannelFacet::SetOverlayExtra(unsigned long Key, float* Extra)
{
    return ChannelSetOverlayExtra(this, Key, Extra);
}
unsigned long DACOM_API FAnimationChannelFacet::InMixer(unsigned long Key)
{
    return ChannelInMixer(this, Key);
}
unsigned long* DACOM_API FAnimationChannelFacet::GetHeader(unsigned long* Out, unsigned long Key)
{
    return ChannelGetHeader(this, Out, Key);
}
GENRESULT DACOM_API FAnimationChannelFacet::AddOverlayPos(unsigned long Key, float* Delta)
{
    return ChannelAddOverlayPos(this, Key, Delta);
}
GENRESULT DACOM_API FAnimationChannelFacet::GetMapped(unsigned long Key, unsigned char* Out)
{
    return ChannelGetMapped(this, Key, Out);
}

GENRESULT DACOM_API FAnimationObject::Initialize()
{
    return AggregateInitialize((IAggregateComponent*)this);     // LAB_06616d40
}

unsigned char DACOM_API FAnimationObject::Eval(unsigned long PlaybackId, void* TouchArg,
                                               unsigned long Flags, float Time, float Scale,
                                               void* Arg6, void* Arg7, float Arg8)
{
    return RootDataEval((IRootData*)this, PlaybackId, TouchArg, Flags, Time, Scale,
                        Arg6, Arg7, Arg8);                                   // LAB_06614b30
}

// LAB_06625380 (RET 0xC): size != 8 or a name that differs from the factory's (inline strcmp)
GENRESULT DACOM_API EngineFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeEngine || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FEngineObject* Object = (FEngineObject*)(std::calloc(1, sizeof(FEngineObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    EngineConstruct(Object);
    Object->RefCount = 1;
    const GENRESULT Hook = EngBaseAggregationHook(Desc);
    if (Hook != 0)
    {
        EngineDestruct(Object);
        std::free(Object);
        *Out = nullptr;
        return Hook;
    }
    *Out = Object;
    return GR_OK;
}

// FUN_0661a750 (RET 0xC). No checks of This / Desc / Out.
GENRESULT DACOM_API AnimationFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeAnimation || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FAnimationObject* Object = (FAnimationObject*)(std::calloc(1, sizeof(FAnimationObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    new (Object) FAnimationObject(); // the four facet tables and the inner unknown's table
    Object->NextPlaybackId = G_AnimFirstPlaybackId;
    // Retail constructs the three trees in place (+0x14 playback map, +0x28 set, +0x3C map;
    // FUN_06616c10); the port holds them through pointers (see FAnimationObject) and frees
    // them in DestroyAnimationLists.
    Object->Playbacks = new FAnimPlaybackMap;
    Object->Channels = new FTypedChannelSet;
    Object->Mixers = new FMixerKeyMap;
    // Inner unknown {table 0662946c (installed by the placement new above), 1, object}.
    Object->Inner.RefCount = 1;
    Object->Inner.Object = Object;

    FAggDesc18* Agg = (FAggDesc18*)Desc;
    IDAComponent* const InnerUnknown = &Object->Inner;
    Object->Controller = (IDAComponent*)(Agg->Inner);
    if (Agg->Inner != nullptr)
        *Agg->Outer = InnerUnknown;
    else
        Object->Controller = InnerUnknown;

    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        if (Agg->Outer != nullptr)
            *Agg->Outer = nullptr;
        DestroyAnimationLists(Object);
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }

    *Out = Object;
    return GR_OK;
}

// FUN_06627c40 (RET 0xC). No checks of This / Desc / Out.
GENRESULT DACOM_API HardpointFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeHardpoint || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FHardpointObject* Object =
        (FHardpointObject*)(std::calloc(1, sizeof(FHardpointObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    HardpointConstruct(Object);
    // Inner unknown {table 06629838 (installed by HardpointConstruct's placement new), 1, object}.
    Object->Inner.RefCount = 1;
    Object->Inner.Object = Object;

    FAggDesc18* Agg = (FAggDesc18*)Desc;
    IDAComponent* const InnerUnknown = &Object->Inner;
    Object->Controller = (IDAComponent*)(Agg->Inner);
    if (Agg->Inner == nullptr)
        Object->Controller = InnerUnknown;
    else
        *Agg->Outer = InnerUnknown;

    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        if (Agg->Outer != nullptr)
            *Agg->Outer = nullptr;
        HardpointDestruct(Object);
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }

    *Out = Object;
    return (GENRESULT)HookResult;
}

// LAB_0661e800, RET 0xC. No null checks of This / Desc / Out.
GENRESULT DACOM_API ChannelArchetypeFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc,
                                                         void** Out)
{
    if (Desc->size != G_AggDescSizeChannelArchetype ||
        std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FChannelArchetypeObject* Object =
        (FChannelArchetypeObject*)(std::calloc(1, sizeof(FChannelArchetypeObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    new (Object) FChannelArchetypeObject();
    ChannelArchetypeInnerCtor(Object);
    Object->RefCount = 1;
    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        ChannelArchetypeTeardown(Object);
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }
    *Out = Object;
    return (GENRESULT)HookResult;
}


static void* NestedThis(void* Object, unsigned long Offset)
{
    return (unsigned char*)Object + Offset;
}

GENRESULT DACOM_API FEngineObject::QueryInterface(const char* Iid, void** Out)
{
    return EngineQueryInterface(this, Iid, Out);
}
unsigned long DACOM_API FEngineObject::AddRef()
{
    return EngineAddRef(this);
}
unsigned long DACOM_API FEngineObject::Release()
{
    return EngineRelease(this);
}
GENRESULT DACOM_API FEngineObject::LoadEngineComponents(IDAComponent* SystemContainer)
{
    return EngineLoadEngineComponents(this, SystemContainer);
}
unsigned long DACOM_API FEngineObject::EnumerateEngineComponents(
    int(__cdecl* Callback)(const char*, void*))
{
    return EngineEnumerateEngineComponents(this, Callback);
}
GENRESULT DACOM_API FEngineObject::NotifyEngineComponents(void* Arg)
{
    return EngineNotifyEngineComponents(this, Arg);
}
GENRESULT DACOM_API FEngineObject::SetSearchPath(const char* Path)
{
    return EngineSetSearchPath(this, Path);
}
GENRESULT DACOM_API FEngineObject::QuerySearchPath(char* Dest, unsigned long MaxChars)
{
    return EngineQuerySearchPath(this, Dest, MaxChars);
}
GENRESULT DACOM_API FEngineObject::SetFilesystem(IDAComponent* NewPath)
{
    return EngineSetFilesystem(this, NewPath);
}
GENRESULT DACOM_API FEngineObject::GetSearchPath(void** Out)
{
    return EngineGetSearchPath(this, Out);
}
GENRESULT DACOM_API FEngineObject::CreateFileSystem(const char* Path, void** Out, void* Factory)
{
    return EngineCreateFileSystem(this, Path, Out, Factory);
}
void* DACOM_API FEngineObject::CreateCompoundArchetype(FEngineName* Name)
{
    return EngineCreateCompoundArchetype(this, Name);
}
void* DACOM_API FEngineObject::CreateArchetypeHandle(FEngineName* Name, void* FileSystem)
{
    return EngineCreateArchetypeHandle(this, Name, FileSystem);
}
void* DACOM_API FEngineObject::DuplicateArchetype(void* ArchIn, FEngineName* Name)
{
    return EngineDuplicateArchetype(this, (FArchetypeHandle*)ArchIn, Name);
}
unsigned long DACOM_API FEngineObject::AddRefHandle(void* Handle)
{
    return EngineAddRefHandle(this, (FArchetypeHandle*)Handle);
}
unsigned long DACOM_API FEngineObject::ReleaseHandle(void* Handle)
{
    return EngineReleaseHandle(this, (FArchetypeHandle*)Handle);
}
FEngineName* DACOM_API FEngineObject::GetArchetypeName(FEngineName* Out, void* Handle)
{
    return EngineGetArchetypeName(this, Out, (FArchetypeHandle*)Handle);
}
void* DACOM_API FEngineObject::FindArchetype(FEngineName* Name)
{
    return EngineFindArchetype(this, Name);
}
GENRESULT DACOM_API FEngineObject::QueryEngineComponent(int Id, void* Name, void** Out)
{
    return EngineQueryEngineComponent(this, Id, Name, Out);
}
unsigned long DACOM_API FEngineObject::IsCompoundArchetype(void* Handle)
{
    return EngineIsCompoundArchetype(this, (FArchetypeHandle*)Handle);
}
GENRESULT DACOM_API FEngineObject::EnumerateArchetypeConstraints(
    void* Handle, int(__cdecl* Callback)(void*, void*, void*, void*), void* Context)
{
    return EngineEnumerateArchetypeConstraints(this, (FArchetypeHandle*)Handle,
                                               Callback, Context);
}
GENRESULT DACOM_API FEngineObject::EnumerateArchetypeChildren(
    void* Handle, int(__cdecl* Callback)(void*, void*, int, void*), void* Context)
{
    return EngineEnumerateArchetypeChildren(this, (FArchetypeHandle*)Handle, Callback,
                                               Context);
}
void* DACOM_API FEngineObject::CreateInstanceFromName(FEngineName* Name, void* FileSystem,
                                                      void* Transform)
{
    return EngineCreateInstanceFromName(this, Name, FileSystem, Transform);
}
void* DACOM_API FEngineObject::CreateInstance(void* Handle, void* Transform)
{
    return EngineCreateInstance(this, (FArchetypeHandle*)Handle, Transform);
}
GENRESULT DACOM_API FEngineObject::DestroyInstance(void* Instance)
{
    return EngineDestroyInstance(this, (FEngineInstance*)Instance);
}
void* DACOM_API FEngineObject::GetInstanceArchetype(void* Instance)
{
    return EngineGetInstanceArchetype(this, (FEngineInstance*)Instance);
}
FEngineName* DACOM_API FEngineObject::GetInstanceName(FEngineName* Out, void* Instance)
{
    return EngineGetInstanceName(this, Out, (FEngineInstance*)Instance);
}
GENRESULT DACOM_API FEngineObject::UpdateInstance(void* Instance, unsigned long Flags, void* Arg)
{
    return EngineUpdateInstance(this, (FEngineInstance*)Instance, Flags, Arg);
}
GENRESULT DACOM_API FEngineObject::SetInstanceTransform(void* Instance, void* Transform)
{
    return EngineSetInstanceTransform(this, (FEngineInstance*)Instance,
                                       (IInstanceTransform*)Transform);
}
GENRESULT DACOM_API FEngineObject::GetInstanceTransformObject(void* Instance, void** Out)
{
    return EngineGetInstanceTransformObject(this, (FEngineInstance*)Instance,
                                            (IInstanceTransform**)Out);
}
GENRESULT DACOM_API FEngineObject::QueryInstance(void* Instance, const char* Iid, void** Out)
{
    return EngineQueryInstance(this, (FEngineInstance*)Instance, Iid, Out);
}
GENRESULT DACOM_API FEngineObject::SetInstanceUserData(void* Instance, void* Value)
{
    return EngineSetInstanceUserData(this, (FEngineInstance*)Instance, Value);
}
void* DACOM_API FEngineObject::GetInstanceUserData(void* Instance)
{
    return EngineGetInstanceUserData(this, (FEngineInstance*)Instance);
}
GENRESULT DACOM_API FEngineObject::SetInstanceExtent(void* Instance, unsigned long Flags, float Extent,
                                                      const float* Vec)
{
    return EngineSetInstanceExtent(this, (FEngineInstance*)Instance, Flags, Extent, Vec);
}
GENRESULT DACOM_API FEngineObject::GetInstanceExtent(void* Instance, unsigned long Flags,
                                                      float* OutExtent, float* OutVec)
{
    return EngineGetInstanceExtent(this, (FEngineInstance*)Instance, Flags, OutExtent,
                                    OutVec);
}
GENRESULT DACOM_API FEngineObject::SetInstancePosition(void* Instance, void* Position)
{
    return EngineSetInstancePosition(this, (FEngineInstance*)Instance, Position);
}
float* DACOM_API FEngineObject::GetInstancePosition(void* Instance)
{
    return EngineGetInstancePosition(this, (FEngineInstance*)Instance);
}
GENRESULT DACOM_API FEngineObject::SetInstanceOrientation(void* Instance, void* Orient)
{
    return EngineSetInstanceOrientation(this, (FEngineInstance*)Instance, Orient);
}
float* DACOM_API FEngineObject::GetInstanceOrientation(void* Instance)
{
    return EngineGetInstanceOrientation(this, (FEngineInstance*)Instance);
}
GENRESULT DACOM_API FEngineObject::SetInstanceMatrix(void* Instance, void* Matrix)
{
    return EngineSetInstanceMatrix(this, (FEngineInstance*)Instance, Matrix);
}
float* DACOM_API FEngineObject::GetInstanceTransform(void* Instance)
{
    return EngineGetInstanceTransform(this, (FEngineInstance*)Instance);
}
// formerly UnusedSlot41
GENRESULT DACOM_API FEngineObject::NoOp41(void* A, void* B)
{
    return StubRetC(this, A, B);
}
float* DACOM_API FEngineObject::GetIdentityMatrix(void* Unused)
{
    return EngineGetIdentityMatrix(this, Unused);
}
// formerly UnusedSlot43
GENRESULT DACOM_API FEngineObject::NoOp43(void* A, void* B)
{
    return StubRetC(this, A, B);
}
float* DACOM_API FEngineObject::GetIdentityMatrix2(void* Unused)
{
    return EngineGetIdentityMatrix(this, Unused);
}
GENRESULT DACOM_API FEngineObject::SetTransformUser(void* Instance, unsigned long User)
{
    return EngineSetTransformUser(this, (FEngineInstance*)Instance,
                                   (void*)User);
}
unsigned long DACOM_API FEngineObject::GetTransformUser(void* Instance)
{
    return EngineGetTransformUser(this, (FEngineInstance*)Instance);
}
unsigned long DACOM_API FEngineObject::CreateJoint(void* Parent, void* Child,
                                                     const unsigned long* Payload)
{
    return EngineCreateJoint(this, (FEngineInstance*)Parent,
                              (FEngineInstance*)Child, Payload);
}
unsigned long DACOM_API FEngineObject::DestroyJoint(void* Parent, void* Child)
{
    return EngineDestroyJoint(this, (FEngineInstance*)Parent,
                              (FEngineInstance*)Child);
}
unsigned long* DACOM_API FEngineObject::GetInstanceRelative(void* Instance)
{
    return EngineGetInstanceRelative(this, (FEngineInstance*)Instance);
}
unsigned long DACOM_API FEngineObject::SetJointParams(void* Instance, unsigned long Index,
                                                      const float* Values)
{
    return EngineSetJointParams(this, (FEngineInstance*)Instance, Index, Values);
}
unsigned long DACOM_API FEngineObject::GetJointParams(void* Instance, unsigned long Index, float* Out)
{
    return EngineGetJointParams(this, (FEngineInstance*)Instance, Index, Out);
}
void* DACOM_API FEngineObject::FindMappedInstance(void* Instance)
{
    return EngineFindMappedInstance(this, (FEngineInstance*)Instance);
}
void* DACOM_API FEngineObject::GetInstanceParent(void* Instance)
{
    return EngineGetInstanceParent(this, (FEngineInstance*)Instance);
}
void* DACOM_API FEngineObject::GetRelatedInstance(void* From, unsigned long Flags, void* Cursor)
{
    return EngineGetRelatedInstance(this, (FEngineInstance*)From, Flags,
                                    (FEngineInstance*)Cursor);
}
void* DACOM_API FEngineObject::GetInstanceRoot(void* Instance)
{
    return EngineGetInstanceRoot(this, (FEngineInstance*)Instance);
}
void* DACOM_API FEngineObject::FindArchetypeChild(void* Handle, void* ChildHandle)
{
    return EngineFindArchetypeChild(this, (FArchetypeHandle*)Handle,
                                    (FArchetypeHandle*)ChildHandle);
}
// formerly UnusedSlot57
unsigned long DACOM_API FEngineObject::RejectOperation(void* A, void* B)
{
    return RejectEngineOperation(this, A, B);
}

GENRESULT DACOM_API FAnimationObject::QueryInterface(const char* Iid, void** Out)
{
    return AnimationQueryInterface(this, Iid, Out);
}
unsigned long DACOM_API FAnimationObject::AddRef()
{
    return AnimationAddRef(this);
}
unsigned long DACOM_API FAnimationObject::Release()
{
    return AnimationRelease(this);
}
void* DACOM_API FAnimationObject::OpenScript(void* FileSystem)
{
    return AnimationOpenScript(this, FileSystem);
}
GENRESULT DACOM_API FAnimationObject::CloseScript(void* Script)
{
    return AnimationCloseScript(this, (FAnimScript*)Script);
}
// Slots 5, 10 and 17 take a playback id, not a script (FUN_06615160 / LAB_06614a50 /
// LAB_066148d0 all open with FUN_06614580 or FUN_06612e70 on the +0x14 map); the shared
// header still carries the dword as void*.
float DACOM_API FAnimationObject::GetScriptDuration(void* Script)
{
    return AnimationGetScriptDuration(this, (unsigned long)((size_t)Script));
}
float DACOM_API FAnimationObject::GetNamedDuration(void* Script, const char* Name)
{
    return AnimationGetNamedDuration(this, (FAnimScript*)Script, Name);
}
GENRESULT DACOM_API FAnimationObject::EnumScriptNames(void* Callback, void* Script, void* Context)
{
    return AnimationEnumScriptNames(this, Callback, (FAnimScript*)Script, Context);
}
GENRESULT DACOM_API FAnimationObject::FindScriptName(void* Callback, void* Script, const char* Name,
                                                     void* Context)
{
    return AnimationFindScriptName(this, Callback, (FAnimScript*)Script, Name, Context);
}
unsigned long DACOM_API FAnimationObject::PlayScript(void* Script, void* Root, const char* Name,
                                                      void* ArgD, void* ArgE)
{
    return AnimationPlayScript(this, (FAnimScript*)Script,
                                 (FEngineInstance*)Root, Name, ArgD, ArgE);
}
GENRESULT DACOM_API FAnimationObject::DestroyScript(void* Script)
{
    return AnimationDestroyScript(this, (unsigned long)((size_t)Script));
}
unsigned long DACOM_API FAnimationObject::EvalPlayback(unsigned long PlaybackId, unsigned long Flags,
                                                     float Time, float Scale, void* A, void* B,
                                                     void* C, void* D)
{
    return AnimationEvalPlayback(this, PlaybackId, Flags, Time, Scale, A, B, C, D);
}
unsigned long DACOM_API FAnimationObject::EvalStored(unsigned long PlaybackId)
{
    return AnimationEvalStored(this, PlaybackId);
}
unsigned long DACOM_API FAnimationObject::ApplyChannels(unsigned long PlaybackId)
{
    return AnimationApplyChannels(this, PlaybackId);
}
unsigned long DACOM_API FAnimationObject::StopPlayback(unsigned long PlaybackId)
{
    return AnimationStopPlayback(this, PlaybackId);
}
unsigned long DACOM_API FAnimationObject::StopChannels(unsigned long PlaybackId, void* Unused)
{
    return AnimationStopChannels(this, PlaybackId, Unused);
}
float DACOM_API FAnimationObject::GetStoredDuration(unsigned long PlaybackId)
{
    return AnimationGetStoredDuration(this, PlaybackId);
}
unsigned long DACOM_API FAnimationObject::FilterObjects(void* Filter)
{
    return AnimationFilterObjects(this, (unsigned long)((size_t)Filter));
}
unsigned long DACOM_API FAnimation2Facet::SetOverlayPos(unsigned long PlaybackId, float* Pos)
{
    return AnimationSetOverlayPos((FAnimationObject*)this, PlaybackId, Pos);
}
unsigned long DACOM_API FAnimation2Facet::SetOverlayExtra(unsigned long PlaybackId, float* Extra)
{
    return AnimationSetOverlayExtra((FAnimationObject*)this, PlaybackId, Extra);
}
unsigned long DACOM_API FAnimationObject::HasObject(unsigned long PlaybackId, void* Object)
{
    return AnimationHasObject(this, PlaybackId, Object);
}
unsigned long DACOM_API FAnimationObject::TouchMapped(unsigned long PlaybackId, float* Pos, float* Extra)
{
    return AnimationTouchMapped(this, PlaybackId, Pos, Extra);
}
unsigned long DACOM_API FAnimation2Facet::AddOverlayPos(unsigned long PlaybackId, float* Delta)
{
    return AnimationAddOverlayPos((FAnimationObject*)this, PlaybackId, Delta);
}
unsigned long DACOM_API FAnimationObject::GetFirstInstanceField(unsigned long PlaybackId,
                                                               unsigned long* Out)
{
    return AnimationGetFirstInstanceField(this, PlaybackId, Out);
}
unsigned long DACOM_API FAnimationObject::GetDurationIfNamed(unsigned long PlaybackId, float* Out)
{
    return AnimationGetDurationIfNamed(this, PlaybackId, Out);
}
unsigned long DACOM_API FAnimationObject::GetNamedDurationOut(void* Script, const char* Name, float* Out)
{
    return AnimationGetNamedDurationOut(this, (FAnimScript*)Script, Name, Out);
}
unsigned long DACOM_API FAnimationObject::FindObject(unsigned long PlaybackId, void* Query)
{
    return AnimationFindObject(this, PlaybackId, Query);
}
unsigned long DACOM_API FAnimationObject::MixerWalk(unsigned long PlaybackId, float Time)
{
    return AnimationMixerWalk(this, PlaybackId, Time);
}
unsigned long DACOM_API FAnimationObject::MixerWalkAll(float Time)
{
    return AnimationMixerWalkAll(this, Time);
}
void* DACOM_API FAnimationObject::CreateEmptyScript()
{
    return AnimationCreateEmptyScript(this);
}
unsigned long DACOM_API FAnimationObject::AddScriptNames(void* Script, void* FileSystem,
                                                       const char** Names, int Count)
{
    return AnimationAddScriptNames(this, (FAnimScript*)Script, FileSystem, Names, Count);
}
unsigned long DACOM_API FAnimationObject::RemoveScriptNames(void* Script, const char** Names, int Count)
{
    return AnimationRemoveScriptNames(this, (FAnimScript*)Script, Names, Count);
}
unsigned long DACOM_API FAnimationObject::EnumFsScripts(void* Callback, void* FileSystem, void* Context)
{
    return AnimationEnumFsScripts(this, Callback, FileSystem, Context);
}
unsigned long DACOM_API FAnimationObject::HasScriptName(void* Script, const char* Name)
{
    return AnimationHasScriptName(this, (FAnimScript*)Script, Name);
}
unsigned long DACOM_API FAnimationObject::GetScriptNameCount(void* Script, unsigned long* Out)
{
    return AnimationGetScriptNameCount(this, (FAnimScript*)Script, Out);
}
unsigned long DACOM_API FAnimationObject::EnumObjects(void* Callback, unsigned long PlaybackId,
                                                       void* Context)
{
    return AnimationEnumObjects(this, Callback, PlaybackId, Context);
}
unsigned long DACOM_API FAnimationObject::RegisterHandle(unsigned long Key)
{
    return AnimationRegisterHandle(this, Key);
}
unsigned long DACOM_API FAnimationObject::HasHandle(unsigned long Key, unsigned char* Out)
{
    return AnimationHasHandle(this, Key, Out);
}
unsigned long DACOM_API FAnimationObject::HasAnyHandle(unsigned long Key)
{
    return AnimationHasAnyHandle(this, Key);
}

GENRESULT DACOM_API FHardpointObject::QueryInterface(const char* Iid, void** Out)
{
    return HardpointQueryInterface(this, Iid, Out);
}
unsigned long DACOM_API FHardpointObject::AddRef()
{
    return HardpointAddRef(this);
}
unsigned long DACOM_API FHardpointObject::Release()
{
    return HardpointRelease(this);
}
unsigned long DACOM_API FHardpointObject::GetHardpoint(void* Handle, FHardpointKey const* Key,
                                                       void* Out)
{
    return HardpointGetHardpoint(this, (FArchetypeHandle*)Handle, Key, Out);
}
unsigned long DACOM_API FHardpointObject::AddHardpoint(void* Handle, FHardpointKey const* Key,
                                                       const void* Data)
{
    return HardpointAddHardpoint(this, (FArchetypeHandle*)Handle, Key, Data);
}
GENRESULT DACOM_API FHardpointObject::EnumerateHardpoints(FHardpointEnumProc Callback,
                                                          void* Handle, void* Context)
{
    return HardpointEnumerateHardpoints(this, Callback, (FArchetypeHandle*)Handle,
                                         Context);
}
unsigned long DACOM_API FHardpointObject::ConnectHardpoints(void* InstA, FHardpointKey const* KeyA,
                                                            void* InstB, FHardpointKey const* KeyB)
{
    return HardpointConnect(this, (FEngineInstance*)InstA, KeyA,
                                  (FEngineInstance*)InstB, KeyB);
}
char const* DACOM_API FHardpointObject::FindHardpoint(void* Handle, unsigned long const* Crc)
{
    return HardpointFindHardpoint(this, (FArchetypeHandle*)Handle, Crc);
}
int DACOM_API FHardpointObject::Initialize()
{
    return (int)(HardpointEngineComponentInitialize(NestedThis(this, 4)));
}
int DACOM_API FHardpointObject::CreateArchetype(void* Arg1, void* Arg2)
{
    return (int)(
        HardpointCreateArchetype(NestedThis(this, 4), (FArchetypeHandle*)Arg1, Arg2));
}
int DACOM_API FHardpointObject::DuplicateArchetype(void* Arg1, void* Arg2)
{
    return (int)(
        HardpointDuplicateArchetype(NestedThis(this, 4), (FArchetypeHandle*)Arg1,
                                        (FArchetypeHandle*)Arg2));
}
int DACOM_API FHardpointObject::DestroyArchetype(void* Arg1)
{
    return (int)(HardpointDestroyArchetype(NestedThis(this, 4),
                                                       (FArchetypeHandle*)Arg1));
}
int DACOM_API FHardpointObject::QueryArchetype(void* Arg1, const char* Iid, void** Out)
{
    return (int)(RejectHardpointQuery(NestedThis(this, 4), Arg1, Iid, Out));
}
int DACOM_API FHardpointObject::CreateInstance(void* Arg1, void* Arg2)
{
    return (int)(NoOpHardpointInstance(NestedThis(this, 4), Arg1, Arg2));
}
int DACOM_API FHardpointObject::DestroyInstance(void* Arg1)
{
    return (int)(StubRet8(NestedThis(this, 4), Arg1));
}
int DACOM_API FHardpointObject::UpdateInstance(void* Arg1, void* Arg2)
{
    return (int)(StubRetC(NestedThis(this, 4), Arg1, Arg2));
}
int DACOM_API FHardpointObject::QueryInstance(void* Arg1, const char* Iid, void** Out)
{
    return (int)(RejectHardpointQuery(NestedThis(this, 4), Arg1, Iid, Out));
}
int DACOM_API FHardpointObject::Notify(void* Arg1)
{
    return (int)(StubRet8(NestedThis(this, 4), Arg1));
}

GENRESULT DACOM_API FChannelArchetypeObject::QueryInterface(const char* Iid, void** Out)
{
    return ChannelArchetypeQueryInterface(this, Iid, Out);
}
unsigned long DACOM_API FChannelArchetypeObject::AddRef()
{
    return ChannelArchetypeAddRef(this);
}
unsigned long DACOM_API FChannelArchetypeObject::Release()
{
    return ChannelArchetypeRelease(this);
}
GENRESULT DACOM_API FChannelArchetypeObject::Load(void* FileSystem)
{
    return ChannelArchetypeLoad(this, FileSystem);
}
GENRESULT DACOM_API FChannelArchetypeObject::GetDuration(float* Out)
{
    return ChannelArchetypeGetDuration(this, Out);
}
GENRESULT DACOM_API FChannelArchetypeObject::GetFlags(unsigned long* Out)
{
    return ChannelArchetypeGetFlags(this, Out);
}
GENRESULT DACOM_API FChannelArchetypeObject::Sample(float Time, const unsigned char** OutA,
                                                     const unsigned char** OutB, float* Alpha)
{
    return ChannelArchetypeSample(this, Time, OutA, OutB, Alpha);
}
GENRESULT DACOM_API FChannelArchetypeObject::SamplePair(float Time, const unsigned char** OutLo,
                                                         const unsigned char** OutHi, float* TimeLo,
                                                         float* TimeHi, float* Alpha)
{
    return ChannelArchetypeSamplePair(this, Time, OutLo, OutHi, TimeLo, TimeHi, Alpha);
}
GENRESULT DACOM_API FChannelArchetypeObject::GetFrame(unsigned char bLast, const unsigned char** Out)
{
    return ChannelArchetypeGetFrame(this, bLast, Out);
}
GENRESULT DACOM_API FChannelArchetypeObject::CollectRange(float Time0, float Time1, float* OutKeys,
                                                          unsigned long Capacity, unsigned long* Count)
{
    return ChannelArchetypeCollectRange(this, Time0, Time1, OutKeys, Capacity, Count);
}
GENRESULT DACOM_API FChannelArchetypeObject::CollectAt(float Time, float* OutKeys,
                                                        unsigned long Capacity, unsigned long* Count)
{
    return ChannelArchetypeCollectAt(this, Time, OutKeys, Capacity, Count);
}

struct FEngineFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};
struct FAnimationFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};
struct FHardpointFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};
struct FChannelArchetypeFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};

GENRESULT DACOM_API FEngineFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return EngineFactoryCreateInstance(this, Desc, Out);
}
GENRESULT DACOM_API FAnimationFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return AnimationFactoryCreateInstance(this, Desc, Out);
}
GENRESULT DACOM_API FHardpointFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return HardpointFactoryCreateInstance(this, Desc, Out);
}
GENRESULT DACOM_API FChannelArchetypeFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return ChannelArchetypeFactoryCreateInstance(this, Desc, Out);
}


static void RegisterFactory(FDacomFactory* Factory, const char* Name, unsigned long Priority)
{
    if (Factory == nullptr)
        return;
    Factory->RefCount = 1;
    Factory->Name = Name;
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom != nullptr)
    {
        Dacom->RegisterComponent((IDAComponent*)Factory, Name,
                                 (void*)Priority);
        Factory->Release();
    }
    else
    {
        Factory->Release();
    }
}

void RegisterEngBaseComponents()
{
    FAnimationFactory* Animation =
        (FAnimationFactory*)(std::calloc(1, sizeof(FAnimationFactory)));
    if (Animation != nullptr)
    {
        new (Animation) FAnimationFactory();
        RegisterFactory(Animation, G_NameAnimation, G_PriorityAnimation);
    }
    FChannelArchetypeFactory* ChannelArch = (FChannelArchetypeFactory*)(
        std::calloc(1, sizeof(FChannelArchetypeFactory)));
    if (ChannelArch != nullptr)
    {
        new (ChannelArch) FChannelArchetypeFactory();
        RegisterFactory(ChannelArch, G_NameChannelArchetype, G_PriorityChannelArchetype);
    }
    FHardpointFactory* Hardpoint =
        (FHardpointFactory*)(std::calloc(1, sizeof(FHardpointFactory)));
    if (Hardpoint != nullptr)
    {
        new (Hardpoint) FHardpointFactory();
        RegisterFactory(Hardpoint, G_NameHardpoint, G_PriorityHardpoint);
    }
    FEngineFactory* Engine = (FEngineFactory*)(std::calloc(1, sizeof(FEngineFactory)));
    if (Engine != nullptr)
    {
        new (Engine) FEngineFactory();
        RegisterFactory(Engine, G_NameEngine, G_PriorityEngine);
    }
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved)
{
    (void)Reserved;
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);
        RegisterEngBaseComponents();
    }
    return TRUE;
}
