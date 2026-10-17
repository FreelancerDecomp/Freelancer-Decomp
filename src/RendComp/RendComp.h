// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "render/rendcomp.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "readfile.h"
#include "x86math.h"

#include <cstddef>
#include <vector>

static const int kFdumpError = 0x100001;
static const int kFdumpWarning = 0x100002;
static const int kFdumpNotice = 0x100003;
static const char* const G_FdumpFmtError = "%s(%d) : ERROR:General:%s";
static const char* const G_FdumpFmtWarning = "%s(%d) : WARNING:General:%s";
static const char* const G_FdumpFmtNotice = "%s(%d) : NOTICE:General:%s";
// Retail __FILE__ strings, each passed as FDUMP's file argument by the functions of its source file.
// .data 06c37048: the TriMesh plugin dtor FUN_06c21080 (PUSH @ 06c210bb).
static const char* const G_FileTriMesh =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\TriMesh\\TriMesh.cpp";
// .data 06c371b8: the TriMesh loader FUN_06c23fd0 (PUSH @ 06c247d1).
static const char* const G_FileTriMeshArchetype =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\TriMesh\\TriMeshArchetype.cpp";
// .data 06c37a38: every RenderManager FDUMP (06c32185, 06c323b6, 06c32474, 06c324ac, 06c33a64, 06c33aa6).
static const char* const G_FileRenderMgr =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\RenderMgr.cpp";
// .data 06c37550 / 06c37650 / 06c376c8 / 06c37778:
static const char* const G_FileVMeshData =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\VMesh\\VMeshData.cpp";
static const char* const G_FileVMeshLibrary =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\VMesh\\VMeshLibrary.cpp";
static const char* const G_FileVMeshWire =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\VMesh\\VMeshWire.cpp";
static const char* const G_FileVMPartArch =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\VMesh\\VMPartArch.cpp";
// rendcomp.dll .data 06c37450 (the sphere archetype load FUN_06c2aa50).
static const char* const G_FileSphereMeshArchetype =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RendComp\\SphereMesh\\SphereMeshArchetype.cpp";

static const unsigned long G_RegisterPriority = 0x80000000;
static const unsigned long G_HandleFail = 0xFFFFFFFF;
static const float G_DefaultLodDistance = 1000000.0f;

struct FDacomFactory;
typedef GENRESULT (DACOM_API* FDacomFactoryCreateFn)(FDacomFactory* This, AGGDESC* Desc, void** Out);

// The 0xC-byte factory every RendComp DllMain registrar allocates: {vptr, RefCount, Name}.
// Retail gives each factory its own four-slot vtable sharing slots 0-2 (LAB_06c221d0 QI,
// LAB_06c306b0 AddRef, FUN_06c2a3a0 Release) and differing only in slot 3 CreateInstance.
// DACOM (ICOManager::RegisterComponent) holds and calls it as an IComponentFactory
// (src/include/dacom.h). Until 2026-10-16 this was a plain struct with a `void** Vtable`
// field pointed at hand-filled `void*[4]` arrays and C-cast to IDAComponent*, i.e. DACOM
// reached it through an interface it did not derive from. The per-factory slot 3 is now the
// Create member, which CreateInstance forwards to.
struct FDacomFactory final : IComponentFactory
{
    explicit FDacomFactory(FDacomFactoryCreateFn CreateIn)
        : RefCount(0), Name(nullptr), Create(CreateIn)
    {
    }

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;

    unsigned long RefCount;
    const char* Name;
    FDacomFactoryCreateFn Create;
};

// The 0x10 descriptor RenderManager's plugin loader (FUN_06c33a10 @ 06c33a39 / 06c33b28..
// 06c33b38) hands DACOM CreateInstance for every [RenderManager] name. The VMesh factory
// (LAB_06c2db50) requires exactly this size, and FUN_06c2d720 requires +8 and +0xC non-null.
struct FRenderPluginDesc
{
    unsigned long Size;         // +0x00 0x10
    const char* Name;           // +0x04 the [RenderManager] line, cut at the first tab/space/';'
    IDAComponent* Parent;       // +0x08 RenderManager +0x28 (the system container)
    IEngine* Engine;            // +0x0C DAT_06c37d4c
};

static_assert(sizeof(FRenderPluginDesc) == 0x10, "render plugin AGGDESC 0x10");

struct FPluginNode
{
    FPluginNode* Next;
    FPluginNode* Prev;
    IRenderComponent* Plugin;
};

// One LOD of a render archetype, retail 8 bytes {handle, plugin}: the entry ctor FUN_06c31e40
// seeds {-1, 0}, FUN_06c32020 / FUN_06c32000 write the two fields, FUN_06c31ff0 indexes them.
// The plugin pointer is a raw copy of the RenderManager plugin list value (no AddRef).
struct FLodEntry
{
    void* Handle;             // +0x00
    IRenderComponent* Plugin; // +0x04
};

// The value of RenderManager's archetype map (retail node+0x10, ctor FUN_06c31e60 zeroes it).
// Retail keeps two raw new[] arrays; the port keeps vectors with the same sizes: Entries holds
// LodCount entries, Switch LodCount + 1 switch distances (FUN_06c31f30).
struct FLodSet
{
    std::vector<FLodEntry> Entries; // +0x00
    int LodCount;                   // +0x04
    int CurrentLod;                 // +0x08
    unsigned char bFlag;            // +0x0C IRenderer Slot19 / Slot21 bit 0
    std::vector<float> Switch;      // +0x10 IRenderer Slot14 / Slot15, FUN_06c32040

    FLodSet() : LodCount(0), CurrentLod(0), bFlag(0) {}
};

void LodSetResize(FLodSet& Set, int Count);             // FUN_06c31f30
void LodSetAssign(FLodSet& Dest, const FLodSet& Src);   // FUN_06c31eb0
FLodEntry* LodSetPick(FLodSet& Set, float Distance);    // FUN_06c32040

struct FVmeshGroup;

// VMeshData, 0x38 bytes: operator new(0x38) + ctor FUN_06c2df60 in FUN_06c2dd20.
struct FVmeshData
{
    unsigned long Crc;          // +0x00 DACOM_CRC::GetCRC32(Name)
    char* Name;                 // +0x04 _strdup of the .vms directory name (case kept)
    IMaterial** Materials;      // +0x08 one per facegroup, zeroed (FUN_06c2dd20)
    void* VertexBuffer;         // +0x0C the group's VB (FUN_06c2d4a0)
    unsigned long Fvf;          // +0x10 the group's FVF
    unsigned long BaseVertex;   // +0x14 first vertex of this mesh inside the group VB
    void* IndexBuffer;          // +0x18 the group's IB (FUN_06c2d5a0)
    unsigned long StartIndex;   // +0x1C first index of this mesh inside the group IB
    FVmeshData* Next;           // +0x20 group list
    FVmeshData* Prev;           // +0x24
    FVmeshGroup* Group;         // +0x28
    int RefCount;               // +0x2C
    float SizeKib;              // +0x30 leaf size * (1/1024), "Memory_VMeshData" LogEvent
    FVmeshDataHeader* Header;   // +0x34 the whole VMeshData leaf, owned
};

// Shared-buffer group, 0x28 bytes: operator new(0x28) + ten zero stores (LAB_06c2ee30).
struct FVmeshGroup
{
    unsigned long VertexTotal;  // +0x00 sum of member Header->Vertices
    unsigned long IndexTotal;   // +0x04 sum of member Header->Indices
    unsigned long Fvf;          // +0x08 0 until the first member is added
    unsigned long Field0C;      // +0x0C TODO: zeroed at construction, no reader in 06c2d330..06c2f480
    unsigned long Field10;      // +0x10 TODO: zeroed at construction, no reader in 06c2d330..06c2f480
    void* VertexBuffer;         // +0x14
    void* IndexBuffer;          // +0x18
    FVmeshData* First;          // +0x1C
    FVmeshData* Last;           // +0x20
    int Count;                  // +0x24
};

// The {name, crc} pair FUN_06c2e720 takes; only +4 is read.
struct FVmeshKey
{
    const char* Name;
    unsigned long Crc;
};

static_assert(offsetof(FVmeshData, Group) == 0x28, "VMeshData+0x28 group");
static_assert(offsetof(FVmeshData, Header) == 0x34, "VMeshData+0x34 header");
static_assert(sizeof(FVmeshData) == 0x38, "VMeshData 0x38");
static_assert(sizeof(FVmeshGroup) == 0x28, "VMesh group 0x28");

// FDUMP(Flags, "%s(%d) : <severity>:General:%s", File, Line, Message) exactly as retail pushes it.
void RendCompFdumpAt(int Flags, const char* File, int Line, const char* Message);
bool RendCompIidEqual(const char* Left, const char* Right);

int FileOpenNode(void* File, const char* Name);
int FileClose(void* File, int Handle);
int FileRead(void* File, int Handle, void* Buffer, unsigned long Size, unsigned long* ReadOut);
unsigned long FileGetSize(void* File, int Handle);
int FileGetCurrentPath(void* File, char* Out, unsigned long MaxChars);
int FileOpenByName(void* File, const char* ChildName);
// FUN_06c24980: the 0x34 FileSystem descriptor (Path, "FileSystem", GENERIC_READ, share 1,
// OPEN_EXISTING, 0x8000080, +0x30 = -1).
FFileSystemDesc* FileDescInit(FFileSystemDesc* Desc, const char* Path, const char* ComponentName);

void* QueryIid(IDAComponent* Object, const char* Iid);
void ReleaseIf(IDAComponent* Object);

// FUN_06c34e60: DACOM_Acquire, then ICOManager +0xC CreateInstance({0xC, "3DMathEngine", 0},
// &Out), and Out is returned. No cache and no checks. Every retail caller caches the result in
// its own global and calls this only while that global is null -- MathEngineCached(Slot).
I3DMathEngine* AcquireMathEngine();

// The four per-caller caches (.data, zero-initialised).
extern I3DMathEngine* G_MathTriMeshDraw; // DAT_06c37cc0: TriMesh draw FUN_06c294f0 (4 sites)
extern I3DMathEngine* G_MathSphere;      // DAT_06c37cc8: sphere draw 06c2b4ff..06c2c145 (9 sites)
extern I3DMathEngine* G_MathVMesh;       // DAT_06c37d44: VMesh draw LAB_06c319e0 (4 sites)
extern I3DMathEngine* G_MathRenderMgr;   // DAT_06c37d48: RenderManager 06c32ba0..06c33111 (6 sites)

// The inline pattern at every call site: `if (Cache == 0) Cache = FUN_06c34e60(); use Cache`.
inline I3DMathEngine* MathEngineCached(I3DMathEngine*& Cache)
{
    if (Cache == nullptr)
        Cache = AcquireMathEngine();
    return Cache;
}

// Materials use the shared vertex/index submission records.

void PipelineApplySubmit(ICamera* Camera, void* Inst, const void* Transform,
                         const FTransform** OutCameraView, const FTransform** OutWorld); // FUN_06c294f0 part
void VectorNegate3(const float* Src, float* Dest);                       // FUN_06c33090
float* TransposeXform3x3(const float* Src12, float* Dest9);             // FUN_06c330b0
float* TransformPoint3(float* Dest3, const float* Matrix12, const float* Vec3); // FUN_06c33100

// VMeshData layer (src/RendComp/VMeshLibrary.cpp).
FVmeshData* VMeshDataLoad(IFileSystem* File, const char* Name);   // FUN_06c2dd20
FVmeshData* VMeshDataFind(const FVmeshKey* Key);                  // FUN_06c2e720
int VMeshDataAddRef(FVmeshData* Data);                            // FUN_06c2dfe0
int VMeshDataRelease(FVmeshData* Data);                           // FUN_06c2dff0
int VMeshGroupEnsureVb(FVmeshGroup* Group);                       // FUN_06c2d4a0
int VMeshGroupEnsureIb(FVmeshGroup* Group, unsigned long BaseVertex); // FUN_06c2d5a0
void VMeshGroupDestroyVb(FVmeshGroup* Group);                     // FUN_06c2d480
void VMeshGroupDestroyIb(FVmeshGroup* Group);                     // FUN_06c2d490

// DAT_06c37ce0 triples {channelId, fvfTestMask, elemByteSize}, filled lazily by the first
// channel query (flag DAT_06c37d70).
struct FVmeshChannelRecord
{
    void* Pointer;              // +0x00 first element of the channel
    int ElemBytes;              // +0x04 table entry +8
    int Stride;                 // +0x08 FVF vertex stride
    int Unwritten0C;            // +0x0C never written by FUN_06c2e020 / FUN_06c31500
    int VertexCount;            // +0x10
};

bool VMeshChannelPresent(unsigned long Fvf, unsigned int ChannelId);
int VMeshChannelByteOffset(unsigned long Fvf, unsigned int ChannelId);
unsigned int VMeshFvfStride(unsigned long Fvf);
// FUN_06c2e020 (VertexStart 0, VertexCount Header->Vertices) and FUN_06c31500 (the VMeshRef's
// range): false and Out untouched when the channel is absent.
bool VMeshFillChannelRecord(const FVmeshDataHeader* Header, unsigned long VertexStart,
                            unsigned long VertexCount, unsigned int ChannelId,
                            FVmeshChannelRecord* Out);
// FUN_06c2e510 / FUN_06c31730: the 0x70 vertex-source descriptor.
void VMeshBuildVertexDesc(const FVmeshDataHeader* Header, unsigned long VertexStart,
                          unsigned long VertexCount, FRpVertexSrcDesc* Out);

// The object-building halves of the TriMesh (LAB_06c22210) and SphereMesh (LAB_06c2a3d0)
// factories, after PluginFactoryCreate's AGGDESC checks (Plugins.cpp).
GENRESULT CreateTriMeshPlugin(const FRenderPluginDesc* Desc, void** Out);
GENRESULT CreateSphereMeshPlugin(const FRenderPluginDesc* Desc, void** Out);

GENRESULT DACOM_API RenderManagerFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out);
GENRESULT DACOM_API VMeshLibraryFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out);
GENRESULT DACOM_API VMeshWireFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out);
GENRESULT DACOM_API VMeshPluginFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out);
GENRESULT DACOM_API PluginFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out);

extern IRenderPipeline* G_Pipeline;
extern IRPDraw* G_Draw;
extern IRPIndexBuffer* G_IndexBuffer;
extern IRPVertexBuffer* G_VertexBuffer;
extern IVertexBufferManager* G_VbManager;
extern IMaterialLibrary* G_MaterialLibrary;
extern IEngine* G_Engine;
