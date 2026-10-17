// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// TriMesh archetype, face groups and UV animation state.

#include "RendComp.h"
#include "render/shading.h" // IMaterialLibrary (archetype +0x21C)

#include <cstddef>
#include <vector>

struct IMaterial;

enum ETriMeshTriangleClass : unsigned long
{
    TriMeshTriangleAllFront = 0x1,
    TriMeshTriangleAllBack = 0x2,
    TriMeshTriangleFrontFrontBack = 0x88,
    TriMeshTriangleFrontBackFront = 0x108,
    TriMeshTriangleFrontBackBack = 0x18,
    TriMeshTriangleBackBackFront = 0x48,
    TriMeshTriangleBackFrontBack = 0x28,
    TriMeshTriangleBackFrontFront = 0x208,
};

// One face group: operator new(0x40) + FUN_06c276a0 (Type 4, Bucket 4, every other dword 0).
// Copy ctor FUN_06c276e0, assignment FUN_06c27780, dtor FUN_06c27720.
struct FTriMeshGroup
{
    static constexpr unsigned long ExtraStreamCount = 4;

    void Initialize(); // formerly TriMeshGroupInit
    void ReleaseResources(); // formerly TriMeshGroupDestroy
    void CopyFrom(const FTriMeshGroup* Source); // formerly TriMeshGroupAssign
    void SetIndices(unsigned long Count, const unsigned short* Source); // formerly TriMeshGroupSetIndices
    void SetEdges(unsigned long Count, const unsigned short* Indices, const float* Angles); // formerly TriMeshGroupSetEdges
    void Rebase(); // formerly TriMeshGroupRebase
    void Unrebase(); // formerly TriMeshGroupUnrebase
    void CopyHeader(const FTriMeshGroup* Source); // formerly TriMeshGroupAssignHeader
    bool HasChannels(unsigned long Channels) const; // formerly TriMeshGroupHasChannels
    void ConvertStripToList(); // formerly TriMeshGroupStripToList
    void PrivatizeVertices(); // formerly TriMeshGroupPrivatize
    void CopyTriangles(const FTriMeshGroup* Source, const unsigned long* Classes, unsigned long Wanted, unsigned long Count); // formerly TriMeshGroupTakeTriangles
    unsigned long ClassifyTriangle(int Triangle, const float* Plane, float PlaneD) const; // formerly TriMeshClassifyTriangle
    void ClipStraddling(FTriMeshGroup* Front, FTriMeshGroup* Back, const unsigned long* Classes, int StraddleCount) const; // formerly TriMeshClipStraddling

    void AllocateExtraStreams(); // formerly TriMeshGroupAllocExtras
    void FreeExtraStreams(); // formerly TriMeshGroupFreeExtras

    unsigned long Type;                // +0x00 D3D primitive type: 4 list, 5 strip
    unsigned long MaterialCrc;         // +0x04 CRC of the lower-cased "Material_name"
    IMaterial* Material;               // +0x08 bound by the hook-up (LAB_06c262a0)
    void* Field_0x0C;                  // +0x0C IMesh Slot16 reads / Slot17 writes it; Draw puts it
                                       //       in submit ctx+0x20. TODO: meaning (no reader here)
    unsigned long MinIndex;            // +0x10 FUN_06c279a0: smallest raw index, subtracted out
    unsigned long MaxIndex;            // +0x14 FUN_06c279a0: largest raw index
    unsigned short* Indices;           // +0x18 "Face_indices" / "Tristrip_indices", rebased
    unsigned long IndexCount;          // +0x1C
    unsigned short* EdgeIndices;       // +0x20 "Edge_indices" (position indices, FUN_06c26700)
    float* EdgeAngles;                 // +0x24 "Edge_angles", EdgeCount / 2 entries
    unsigned long EdgeCount;           // +0x28
    FRpVertexSrcDesc* Vertices;        // +0x2C the vertex source this group draws from
    FRpVertexSrcDesc* SharedVertices;  // +0x30 the archetype's +0x1C; == Vertices when shared
    unsigned long* ExtraIndexCounts;   // +0x34 new(0x10) (FUN_06c29000): per extra stream count
    FRpVertexStream* ExtraStreams;     // +0x38 new(0x50): four streams, IMesh channels 0x4000..
    unsigned long Bucket;              // +0x3C hook-up sort key 0..4 (IMaterial flags)
};

static_assert(sizeof(FTriMeshGroup) == 0x40, "TriMesh face group is 0x40 bytes");

// The UV0 animation block at archetype +0x8C (seven dwords, zeroed by the ctor and the reset).
struct FTriMeshUvAnim
{
    unsigned long LookupCount;  // +0x00 (+0x8C) "UV0_anim_lookup" entries
    long* Lookup;               // +0x04 (+0x90) vertex slot per entry, -1 = skip
    unsigned long FrameCount;   // +0x08 (+0x94) "UV0_frame_count"
    float* Frames;              // +0x0C (+0x98) "UV0_anim": LookupCount (u, v) pairs per frame
    float Fps;                  // +0x10 (+0x9C) "UV0_fps"
    unsigned long Interpolate;  // +0x14 (+0xA0) "UV0_interpolate"
    float Period;               // +0x18 (+0xA4) (FrameCount - 1) / Fps
};

static_assert(sizeof(FTriMeshUvAnim) == 0x1C, "TriMesh UV0 animation block is 0x1C bytes");


// Group-level helpers (cdecl / thiscall in retail).
FTriMeshGroup* TriMeshGroupCopy(const FTriMeshGroup* Source);                // new + FUN_06c276e0
void TriMeshStreamFree(FRpVertexStream* Stream);                             // FUN_06c23dc0
void TriMeshStreamCopy(const FRpVertexStream* Source, FRpVertexStream* Dest,
                       unsigned long IndexCount);                            // FUN_06c24c60
void TriMeshDescFree(FRpVertexSrcDesc* Desc);                                // FUN_06c23f90

class FTriMeshArch : public IMesh
{
public:
    // +0x04: vtable PTR_LAB_06c36134 (4 slots). QI/AddRef/Release are the this-4 thunks
    // LAB_06c27660 / LAB_06c27670 (FUN_06c2b100 = 1); Slot3 is the material hook-up.
    class FHookFacet : public IMatHookUp
    {
    public:
        FTriMeshArch* Owner;

        explicit FHookFacet(FTriMeshArch* InOwner) : Owner(InOwner) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
        int DACOM_API HookUpMaterials() override; // LAB_06c262a0 // formerly Slot3
    };

    // +0x08: vtable PTR_LAB_06c36118 (7 slots). QI/AddRef/Release are the this-8 thunks
    // LAB_06c27680 / LAB_06c27690.
    class FRenderableFacet : public IRenderableMesh
    {
    public:
        FTriMeshArch* Owner;

        explicit FRenderableFacet(FTriMeshArch* InOwner) : Owner(InOwner) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
        int DACOM_API GetVertexCount(int* Out) override;                         // LAB_06c23df0 // formerly Slot3
        int DACOM_API CopyVertexData(void* Dest, unsigned int Fvf, unsigned int StartVertex, unsigned int VertexCount) override; // LAB_06c23e10 // formerly Slot4
        int DACOM_API GetIndexCount(int* Out) override;                         // LAB_06c23e80 // formerly Slot5
        int DACOM_API CopyIndices(unsigned short** Dest, int Count) override;           // LAB_06c23e40 // formerly Slot6
    };

    FHookFacet HookFacet;                  // +0x04
    FRenderableFacet RenderableFacet;      // +0x08
    // +0x0C..+0x18: VC6 std::vector<FTriMeshGroup*> (allocator byte, first, last, end).
    std::vector<FTriMeshGroup*> Groups;
    FRpVertexSrcDesc Vertices;             // +0x1C the shared vertex source (loader, FUN_06c25140)
    FTriMeshUvAnim UvAnim;                 // +0x8C
    unsigned long Flags;                   // +0xA8 bit0 loaded, bit1 sphere cached, bit2 box
                                           //       cached, bit3 centroid computed
    char Name[0x104];                      // +0xAC the node path (IFileSystem +0xA4)
    float Center[3];                       // +0x1B0 FUN_06c25d40
    float Box[6];                          // +0x1BC max x, min x, max y, min y, max z, min z
    float SphereCenter[3];                 // +0x1D4 FUN_06c258c0
    float SphereRadius;                    // +0x1E0 -1.0f after a reset
    void* Field_0x1E4;                     // +0x1E4 FUN_06c265f0 hands it to Draw as a ready
                                           //       vertex listing; only ever zeroed. TODO
    // +0x1E8 the edit cursor, retail a Group** into the vector; the port keeps the index and
    // treats Cursor == Groups.size() as retail's "cursor == end".
    size_t Cursor;
    unsigned long LockedMask;              // +0x1EC channels locked by IMesh Slot20
    unsigned short* Locked[10];            // +0x1F0..+0x214 per-channel locked index copies
    int EditMode;                          // +0x218 IMesh Slot9 mode (2 = private geometry)
    IMaterialLibrary* MaterialLibrary;     // +0x21C DAT_06c37d54 (loader FUN_06c26d90)
    IRenderPipeline* Pipeline;           // +0x220 DAT_06c37d50
    IVertexBufferManager* VbManager;       // +0x224 DAT_06c37d64

    FTriMeshArch();                        // FUN_06c22440
    ~FTriMeshArch();                       // FUN_06c22550

    bool CursorAtEnd() const { return Cursor == Groups.size(); }

    // Non-interface members (thiscall in retail).
    int Reset();                                                     // FUN_06c23ea0
    int Load(IFileSystem* File, IRenderPipeline* InPipeline, IVertexBufferManager* InVbManager,
             IMaterialLibrary* InLibrary, int bTristrips);           // FUN_06c23fd0
    int Clone(FTriMeshArch** Out);                                   // FUN_06c24aa0
    int SplitByPlane(const float* Plane, float PlaneD, unsigned char SplitFlags,
                     FTriMeshArch** OutFront, FTriMeshArch** OutBack); // FUN_06c24d00
    int CompactVertices();                                           // FUN_06c25140
    int GetBox(float* Out);                                          // FUN_06c25790
    int GetSphere(float* OutCenter, float* OutRadius);               // FUN_06c258c0
    int GetCenter(float* Out);                                       // FUN_06c25d40
    int LockChannel(unsigned long Channel);                          // FUN_06c25e30
    int UnlockChannel(unsigned long Channel);                        // FUN_06c26000
    int LockEdgeAngles();                                            // FUN_06c261b0
    int UnlockEdgeAngles();                                          // FUN_06c26200
    int TriangleCount();                                             // FUN_06c26220
    int VertexCount();                                               // FUN_06c26260
    bool GetPrebuiltListing(void** Out);                             // FUN_06c265f0
    int PrivatizeCursorGroup();                                      // FUN_06c26610
    int ShareGroupVertices(bool bAll);                               // FUN_06c26630

    // IDAComponent (FUN_06c26530 / FUN_06c2b100).
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    // IMesh, table PTR_FUN_06c36148.
    int DACOM_API ResetMesh() override;                                           // FUN_06c225f0 // formerly Slot3
    int DACOM_API NoOp() override;                                           // FUN_06c2f5e0 // formerly Slot4
    int DACOM_API RefreshMaterials(unsigned long Flags) override;                        // FUN_06c22620 // formerly Slot5
    int DACOM_API RejectOperation(void* A) override;                                    // LAB_06c22650 // formerly Slot6
    int DACOM_API RejectOperation() override;                                           // FUN_06c22660 // formerly Slot7
    int DACOM_API GetFaceGroupCount(int* Out) override;                                   // LAB_06c22670 // formerly Slot8
    int DACOM_API OpenFaceGroup(int Index, int Mode) override;                        // LAB_06c226a0 // formerly Slot9
    int DACOM_API CloseFaceGroup() override;                                          // FUN_06c226f0 // formerly Slot10
    int DACOM_API AddFaceGroup(void* A) override;                                   // FUN_06c22740 // formerly Slot11
    int DACOM_API RemoveFaceGroup(int Index) override;                                 // FUN_06c227a0 // formerly Slot12
    int DACOM_API GetFaceGroupPrimitiveType(unsigned int* Out) override;                                // LAB_06c22800 // formerly Slot13
    int DACOM_API GetFaceGroupMaterial(IMaterial** Out) override;                           // LAB_06c22830 // formerly Slot14
    int DACOM_API SetFaceGroupMaterial(IMaterial* Material) override;                    // LAB_06c22880 // formerly Slot15
    int DACOM_API GetFaceGroupMaterialContext(void** Out) override;                                // LAB_06c22960 // formerly Slot16
    int DACOM_API SetFaceGroupMaterialContext(void* A) override;                                   // LAB_06c22990 // formerly Slot17
    int DACOM_API OpenFaceGroupChannels(unsigned long Flags) override;                       // FUN_06c229c0 // formerly Slot18
    int DACOM_API CloseFaceGroupChannels() override;                                          // LAB_06c22a00 // formerly Slot19
    int DACOM_API LockFaceGroupChannels(unsigned long Mask) override;                        // LAB_06c22a20 // formerly Slot20
    int DACOM_API UnlockFaceGroupChannels() override;                                          // FUN_06c22c50 // formerly Slot21
    int DACOM_API GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D) override; // LAB_06c22d50 // formerly Slot22
    int DACOM_API SetFaceGroupData(int Channel, void* A, void* B, void* C, void* D) override;     // LAB_06c22fe0 // formerly Slot23
    int DACOM_API GetFaceGroupIndices(int Channel, void** A, void** B) override;           // LAB_06c23650 // formerly Slot24
    int DACOM_API SetFaceGroupIndices(int Channel, void* Src, int Count) override;         // LAB_06c237c0 // formerly Slot25
    int DACOM_API GetFaceGroupIndexCount(int Selector, void** Out) override;                  // LAB_06c23550 // formerly Slot26
    int DACOM_API SetFaceGroupIndexCount(int Selector, int Count) override;                   // FUN_06c235a0 // formerly Slot27
    int DACOM_API AddFaceGroupChannels(unsigned long Flags) override;                       // FUN_06c23970 // formerly Slot28
    int DACOM_API RemoveFaceGroupChannels(unsigned long Flags) override;                       // LAB_06c23bd0 // formerly Slot29
};
