// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// RendComp.dll — documented IIDs and on-disk records (docs/re/render.md).
// Method names describe observed behavior; slot order is unchanged.

#include "dacom.h"
#include "render/IMaterial.h"
#include "luaprofile.h"
#include "render/mesh_iface.h"
#include "engine/engbase.h"


static const char* const G_NameRenderManager = "RenderManager";
static const char* const G_NameVMesh = "VMesh";
static const char* const G_NameTriMesh = "TriMesh";
static const char* const G_NameSphereMesh = "SphereMesh";
static const char* const G_NameVMeshLibrary = "VMeshLibrary";
static const char* const G_NameVMeshWire = "VMeshWire";

// IEngineComponent now has one home: src/include/engine/engbase.h.

// ICamera is declared in common_misc.h (slot 16 +0x40 GetViewTransform is what RendComp reads).
struct ICamera;

struct IRenderer2 : IDAComponent
{

    virtual int DACOM_API QueryGeometry(void* A, float Lod, void* C, void* D, void* E, void* F,
                                void* G) = 0; // formerly Slot3
    virtual int DACOM_API GetBounds(void* Id, float Lod, void* Out) = 0; // formerly Slot4
    virtual int DACOM_API GetCentroid(void* Id, float Lod, void* Out) = 0; // formerly Slot5
    virtual int DACOM_API NoOp(void* A, void* B, void* C, void* D, void* E, void* F,
                                void* G) = 0; // formerly Slot6
    virtual int DACOM_API RenderInstance(void* Camera, void* Inst, float Threshold, unsigned long Flags,
                                void* Matrix, void* Extra) = 0; // formerly Slot7
    virtual int DACOM_API GetInstanceLodCount(void* Inst, void* Out) = 0; // formerly Slot8
    virtual int DACOM_API GetInstanceCurrentLod(void* Inst, void* Out) = 0; // formerly Slot9
    virtual int DACOM_API SetInstanceLod(void* Inst, int Index) = 0; // formerly Slot10
    virtual int DACOM_API GetArchetypeLodCount(void* Id, int* Out) = 0; // formerly Slot11
    virtual int DACOM_API GetArchetypeCurrentLod(void* Id, int* Out) = 0; // formerly Slot12
    virtual int DACOM_API SetArchetypeLod(void* Id, int Index) = 0; // formerly Slot13
    virtual int DACOM_API GetArchetypeLodRange(void* Id, int Index, void* OutA, void* OutB) = 0; // formerly Slot14
    virtual int DACOM_API SetArchetypeLodRange(void* Id, int Index, float A, float B) = 0; // formerly Slot15

    virtual int DACOM_API GetInstanceLodRange(void* Inst, int Index, void* OutA, void* OutB) = 0; // formerly Slot16
    virtual int DACOM_API SetInstanceLodRange(void* Inst, int Index, float A, float B) = 0; // formerly Slot17
    virtual int DACOM_API QueryGeometryAtLod(void* A, void* B, void* C, void* D, void* E, void* F,
                                 void* G) = 0; // formerly Slot18
    virtual int DACOM_API SetArchetypeRootLodSelection(void* Id, void* Unused, unsigned long Flags) = 0; // formerly Slot19
    virtual int DACOM_API SetInstanceRootLodSelection(void* Id, void* A, void* B) = 0; // formerly Slot20
    virtual int DACOM_API GetArchetypeRootLodSelection(void* Key, void* Unused, unsigned long* Out) = 0; // formerly Slot21
    virtual int DACOM_API GetInstanceRootLodSelection(void* Id, void* A, void* B) = 0; // formerly Slot22
};

struct IRenderComponent : IDAComponent
{
    virtual int DACOM_API Claim(void* File) = 0;
    virtual int DACOM_API CloneHandle(void* Handle) = 0; // formerly Slot4
    virtual int DACOM_API DestroyHandle(void* Handle) = 0;
    virtual int DACOM_API QueryHandle(void* Handle, const char* Iid, void** Out) = 0; // formerly Slot6
    virtual int DACOM_API CreateMeshInstance(void* InstId, void* Handle) = 0; // formerly Slot7
    virtual int DACOM_API DestroyMeshInstance(void* InstId, void* Handle) = 0; // formerly Slot8
    virtual int DACOM_API UpdateMeshInstance(void* InstId, void* Arg) = 0; // formerly Slot9
    virtual int DACOM_API QueryMeshInstance(void* InstId, void* Handle, const char* Iid, void** Out) = 0; // formerly Slot10
    virtual int DACOM_API UpdateMeshInstances(void* InstId) = 0; // formerly Slot11
    virtual int DACOM_API QueryGeometry(void* Handle, int Mode, void* A, void* B, void* C, void* D,
                                 void* E) = 0; // formerly Slot12
    virtual int DACOM_API GetBounds(void* Handle, float Lod, void* Out) = 0; // formerly Slot13
    virtual int DACOM_API GetCentroid(void* Handle, float Lod, void* Out) = 0; // formerly Slot14
    virtual int DACOM_API SplitByPlane(void* Handle, void* A, void* B, void* C, void* D, void* E,
                                 void* F) = 0; // formerly Slot15
    virtual int DACOM_API RenderInstance(ICamera* Camera, void* Handle, void* Inst, unsigned long Flags,
                                 void* Matrix, void* Extra) = 0; // formerly Slot16
    virtual int DACOM_API BindMeshInstance(void* Inst, void* Handle) = 0; // formerly Slot17
};

// IMesh — see render/mesh_iface.h (shared with flmaterials.h).

struct IMatHookUp : IDAComponent
{
    virtual int DACOM_API HookUpMaterials() = 0; // formerly Slot3
};

struct IRenderableMesh : IDAComponent
{
    virtual int DACOM_API GetVertexCount(int* Out) = 0; // formerly Slot3
    virtual int DACOM_API CopyVertexData(void* Dest, unsigned int Fvf, unsigned int StartVertex, unsigned int VertexCount) = 0; // formerly Slot4
    virtual int DACOM_API GetIndexCount(int* Out) = 0; // formerly Slot5
    virtual int DACOM_API CopyIndices(unsigned short** Dest, int Count) = 0; // formerly Slot6
};

struct FVmeshRef;
struct FVmeshDataHeader;

// VMesh archetype facet queried as Iids::VMeshLibrary.
// The library component exposes a different facet for the same IID.
struct IVMeshPart : IDAComponent
{
    // +0x0C LAB_06c319e0 (RET 0x14): draw every facegroup of the part.
    virtual bool DACOM_API Draw(ICamera* Camera, void* Inst, unsigned long Flags,
                                const void* Matrix, IMaterial* Override) = 0;
    // +0x10 FUN_06c31950 (RET 8): make the group VB/IB current; fills a 12-byte VB listing
    // {VB, FVF, base vertex} and an 8-byte IB scratch {IB, start index}.
    virtual bool DACOM_API Prepare(void* Listing, void* Scratch) = 0;
    virtual FVmeshRef* DACOM_API GetRef() = 0;               // +0x14 FUN_06c30900
    virtual FVmeshDataHeader* DACOM_API GetHeader() = 0;     // +0x18 FUN_06c30910 (0 w/o data)
};

struct ISphereMesh : IDAComponent
{
    virtual int DACOM_API SetLodDistanceScale(float Value) = 0; // formerly Slot3
    virtual int DACOM_API GetLodDistanceScale(float* Out) = 0; // formerly Slot4
    virtual int DACOM_API NoOp05(void* Arg) = 0; // formerly Slot05
    virtual int DACOM_API NoOp06(void* Arg) = 0; // formerly Slot06
    virtual int DACOM_API SetMaxTessellationLevel(int Value) = 0; // formerly Slot7
    virtual int DACOM_API GetMaxTessellationLevel(int* Out) = 0; // formerly Slot8
    virtual int DACOM_API SetRadius(float Value) = 0; // formerly Slot9
    virtual int DACOM_API GetRadius(float* Out) = 0; // formerly Slot10
    virtual int DACOM_API RequestRebuild(void* Arg) = 0; // formerly Slot11
    virtual int DACOM_API GetMaterialCount(int* OutCount) = 0; // formerly Slot12
    virtual int DACOM_API SetMaterial(int Index, IMaterial* Material) = 0; // formerly Slot13
    virtual int DACOM_API GetMaterial(int Index, IMaterial** Out) = 0; // formerly Slot14
};

struct ITriMeshAnim : IDAComponent
{
    virtual void AdvanceUvAnimation(float Dt) = 0; // formerly Slot3
};

// IVMeshLibrary lives at object+4 (DALib / MaterialLib::Load). 5 slots.
struct IVMeshLibrary : IDAComponent
{
    virtual void* DACOM_API Load(void* File, int LodFilter) = 0;    // +0x0C
    virtual void DACOM_API Unload(void* Handle) = 0;                // +0x10
};

// IVMeshWire — see render/mesh_iface.h (shared with flmaterials.h).

// VMeshData leaf header. Facegroups (0xC each) follow at +0x10, then Indices ushorts, then the
// vertices (stride from Fvf). +0x01..+0x03 are unread (do not invent).
#pragma pack(push, 1)
struct FVmeshDataHeader
{
    unsigned char Flags;        // +0x00 bit0 set = facegroup-relative indices (FUN_06c2d5a0);
                                //        clear = "mesh in old format" (FUN_06c2dd20 refuses it)
    unsigned char Unread01[3];  // +0x01..+0x03 unread
    unsigned long PrimitiveType; // +0x04 IMaterial::Submit PrimType (LAB_06c319e0), IMesh Slot13
    unsigned short FaceGroups;  // +0x08
    unsigned short Indices;     // +0x0A
    unsigned short Fvf;         // +0x0C
    unsigned short Vertices;    // +0x0E
};

// VMeshRef record 0x3C. +0x00 unread. +0x04 is the VMeshData CRC (LAB_06c317b0).
struct FVmeshRef
{
    unsigned long Unread00;     // +0x00 unread
    unsigned long Crc;          // +0x04 VMeshData key (LAB_06c317b0)
    unsigned short VertexStart; // +0x08 FUN_06c31500 / FUN_06c31950
    unsigned short VertexCount; // +0x0A
    unsigned short IndexStart;  // +0x0C FUN_06c31950 / LAB_06c319e0
    unsigned short IndexCount;  // +0x0E IRenderableMesh Slot5/Slot6 (LAB_06c30f70 / LAB_06c30f10)
    unsigned short FacegroupStart; // +0x10
    unsigned short FacegroupCount; // +0x12
    unsigned long Box14[6];     // +0x14 copied out whole by FUN_06c310a0 (IRC Slot13)
    float Extra2C[3];           // +0x2C center (FUN_06c310c0 / FUN_06c310f0)
    float Extra38;              // +0x38 radius (FUN_06c310c0 / SetInstanceExtent)
};

// Facegroup 0xC (header +0x10 + 12 * index).
struct FVmeshFacegroup
{
    unsigned long MaterialCrc;  // +0x00 IMaterialLibrary Slot16 key (LAB_06c317b0)
    unsigned short VertexBegin; // +0x04 added onto each index (FUN_06c2d5a0), Submit Start
    unsigned short VertexEnd;   // +0x06 Submit Count = End - Begin + 1 (LAB_06c319e0)
    unsigned short IndexCount;  // +0x08
    unsigned short Unread0A;    // +0x0A
};

// VWireData header 0x10 before the index array. +0x00/+0x02 unread.
struct FVwireDataHeader
{
    unsigned short Unread00;    // +0x00
    unsigned short Unread02;    // +0x02
    unsigned long Crc;          // +0x04 VMeshData key (FUN_06c2f7e0 / FUN_06c2f820)
    unsigned short IbBias;      // +0x08 added to VMeshData+0x14 for select_ib (FUN_06c2f820)
    unsigned short RemapCount;  // +0x0A unread in RendComp (the exe part cache reads it)
    unsigned short Count;       // +0x0C index count: CopyIndices and the draw count
    unsigned short NumVertices; // +0x0E IRPDraw NumVertices
};
#pragma pack(pop)

static_assert(sizeof(FVmeshDataHeader) == 0x10, "VMeshData header 0x10");

static_assert(sizeof(FVmeshRef) == 0x3C, "VMeshRef 0x3C");
static_assert(sizeof(FVmeshFacegroup) == 0xC, "facegroup 0xC");
static_assert(sizeof(FVwireDataHeader) == 0x10, "VWireData header 0x10");
