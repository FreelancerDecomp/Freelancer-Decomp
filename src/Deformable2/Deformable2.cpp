// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Deformable2.h"
#include "EyeMaterial.h"     // RegisterEyeMaterialFactory (FUN_065fdfd0)
#include "common_misc.h"   // ICamera (GetViewTransform +0x40, ClassifySphere +0x3C)
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include <cstddef>
#include <cstdarg>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>

namespace
{
    void LogDeformError(int Line, const char* Message)
    {
        if (FDUMP != nullptr)
        {
            // .data:06602070 (PE bytes; the listing truncates it): the __FILE__ of the three
            // Initialize sites 065f1b7c (0xB5), 065f1c27 (0xCC), 065f1d3c (0xF4).
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\Deformable.cpp",
                  Line, Message);
        }
    }

    bool IidEquals(const char* Left, const char* Right)
    {
        if (Left == nullptr || Right == nullptr)
            return false;
        return std::strcmp(Left, Right) == 0;
    }

    void ReleaseInterface(IDAComponent*& Pointer)
    {
        if (Pointer == nullptr)
            return;
        Pointer->Release();
        Pointer = nullptr;
    }

    template <typename T>
    void ReleaseInterface(T*& Pointer)
    {
        IDAComponent* Component = Pointer;
        ReleaseInterface(Component);
        Pointer = nullptr;
    }
}

// DAT_06603128 .. DAT_0660314c — module cache written by FUN_065f1810 / FUN_065f1900 / LAB_065f1bf0.
static IDAComponent* G_System = nullptr;
static IMaterialLibrary* G_MaterialLibrary = nullptr; // formerly G_MaterialLib
static IRenderPipeline* G_Pipeline = nullptr;
// EngBase's IEngine, acquired by QI. Every slot this module calls (+0x20, +0x24,
// +0x30, +0x3C, +0x48, +0x60) is IEngine's.
IEngine* G_Engine = nullptr;
// EngBase's IAnimation2, acquired by QI (Iids::Animation2).
static IAnimation2* G_Animation = nullptr;
// EngBase's IChannel, the facet at Animation+8. All five slots this module drives
// (3, 5, 6, 7, 15) match engbase.h's declaration argument for argument.
static IChannel* G_Channel = nullptr;
static IRenderer2* G_Renderer = nullptr;
static IRPVertexBuffer* G_VertexBuffer = nullptr;
static IRPIndexBuffer* G_IndexBuffer = nullptr;
static I3DMathEngine* G_MathEngine = nullptr; // DAT_066033a8 FUN_06600de0
static float G_DeformRate = 0.0f;


// IK current and trial poses; row zero holds the start joint.
static float G_IkCurRot[G_IkMaxJoints + 1][9];
static float G_IkCurPos[G_IkMaxJoints + 1][3];
static float G_IkNewPos[G_IkMaxJoints + 1][3];
static float G_IkNewRot[G_IkMaxJoints + 1][9];

// Source paths used by diagnostics.
static const char* const G_FdumpFileDeformableMesh =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformableMesh.cpp";
static const char* const G_FdumpFileDeformablePart =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformablePart.cpp";
static const char* const G_FdumpFileDeformablePartArchetype =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformablePartArchetype.cpp";
static const char* const G_FdumpFileMat =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\Mat.h";
// .data:06602570 and .data:066026dc, read from the PE bytes (the listing truncates both): the
// __FILE__ and message FUN_065f5400 pushes at line 0x44D.
static const char* const G_FdumpFileDeformableObject =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformableObject.cpp";
static const char* const G_StartAimBlocked = "deform::start_aim(): instance blocked -- exiting";
// .data:066029e8 (PE bytes): the __FILE__ FUN_065fb030 pushes at line 0x15.
static const char* const G_FdumpFileDeformIk =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\deformIK.cpp";
static const char* const G_RateCharset = "0123456789truefalsynoxTRUEFALSYNOX+-";
static const float G_RandToUnit = 3.051851e-05f;
static const int G_PartDescStride = 0x18;
static const int G_MaxLodCount = 8;
static const unsigned long G_ParseTagUtf = 0x112;
// Return to the parent UTF node after loading a child.
static const char* const G_ParentNode = "..";


// DAT_06603158: the mesh cache -- an STL map from CRC32(name) to the mesh (FUN_065f2a40 find,
// FUN_065f2ab0 insert, FUN_065f3440/3320 + FUN_065f2cc0 erase). Both kinds of FDeformMesh
// live in it. The port uses the current std::map; no VC6 layout is kept.
static std::map<unsigned long, FDeformMesh*> G_MeshCache;

static int MeshTotalIndexCount(FMeshArchetype* Arch);
static int MeshVertexCapacity(FMeshArchetype* Arch);
static unsigned char ComputeMeshBoundSphere(FMeshArchetype* Arch, float* OutRadius, float* OutCenter);

// FUN_065f28e0 (thiscall): INC [this+4].
static void AddRefDeformMesh(FDeformMesh* Mesh)
{
    Mesh->RefCount += 1;
}

// FUN_065f28f0 (thiscall), 065f28f0..2a23: DEC [this+4]; still positive -> return it.
static int ReleaseDeformMesh(FDeformMesh* Mesh)
{
    Mesh->RefCount -= 1;
    const int Remaining = Mesh->RefCount;
    if (Remaining > 0)
        return Remaining;
    G_MeshCache.erase(Mesh->NameCrc);
    delete Mesh;
    return Remaining;
}

// PTR_LAB_06601378 -- the base's slots.
FDeformMesh::FDeformMesh(const char* Name)
    : RefCount(1)
    , NameCrc(DACOM_CRC::GetCRC32(Name))
{
}

// LAB_065f8ca0: XOR EAX,EAX; RET.
int FDeformMesh::GetIndexCount()
{
    return 0;
}

// LAB_065f8ca0 (the same body in slot 1).
int FDeformMesh::GetVertexCapacity()
{
    return 0;
}

// LAB_065f8cb0: XOR AL,AL; RET 4.
unsigned char FDeformMesh::FillIndexBuffer(FDeformIbDesc* /*Ib*/)
{
    return 0;
}

// LAB_065f8cc0: XOR AL,AL; RET 0xC -- three arguments, as FUN_065f69d0 pushes them.
unsigned char FDeformMesh::Skin(FLodHolder* /*Holder*/, FDeformVbDesc* /*Vb*/,
                                unsigned char /*bForce*/)
{
    return 0;
}

// LAB_065f8cd0: XOR AL,AL; RET 8.
unsigned char FDeformMesh::GetBoundSphere(float* /*OutRadius*/, float* /*OutCenter*/)
{
    return 0;
}

// FUN_065f8ce0: restores the base vtable, operator delete on bit 0 -- no fields to free.
FDeformMesh::~FDeformMesh()
{
}

// FUN_065f9170 (thiscall, 065f9170..9186): [ECX] non-null -> Release (vt+8), [ECX] = 0. The
// slot pointer itself is not tested (until 2026-09-30 the port returned for a null slot).
static void ReleaseFaceGroupMaterialSlot(IMaterial** MaterialSlot)
{
    if (*MaterialSlot == nullptr)
        return;
    IMaterial* Material = *MaterialSlot;
    Material->Release();
    *MaterialSlot = nullptr;
}

// FUN_065f9430 (thiscall, 065f9430..944b): operator delete(+4), then +4/+8/+0xC = 0. The list
// pointer is not tested (until 2026-09-30 the port returned for a null list).
static void DestroyBoneVertexList(FBoneVertexList* List)
{
    delete[] (unsigned char*)List->Start;
    List->Start = nullptr;
    List->Finish = nullptr;
    List->CapacityEnd = nullptr;
}

// FUN_065f9ba0 (thiscall RET 4, 065f9ba0..9bce) with flags 0: operator delete(+4), +4/+8/+0xC
// = 0; bit 0 of the flags would also delete the list. No test of the list pointer (until
// 2026-09-30 the port returned for a null list).
static void DestroyVertexRemapList(FVertexRemapList* List)
{
    delete[] (unsigned char*)List->Start;
    List->Start = nullptr;
    List->Finish = nullptr;
    List->CapacityEnd = nullptr;
}

// FUN_065f8b90 listing 17402–17511 (reached through FUN_065f8b70, vtable slot 6): restore the
// mesh vtable, free counted FaceGroups/BoneVerts, unconditional delete of stream buffers, 9ba0
// remap walk, FaceFlags, then the base vtable (~FDeformMesh).
FMeshArchetype::~FMeshArchetype()
{
    if (FaceGroups != nullptr)
    {
        int* Header = (int*)FaceGroups - 1;
        int Count = *Header;
        int Index = Count - 1;
        while (Index >= 0)
        {
            FFaceGroup* Group = &FaceGroups[Index];
            delete[] (unsigned char*)Group->Indices;
            ReleaseFaceGroupMaterialSlot(&Group->Material);
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
        FaceGroups = nullptr;
        FaceGroupCount = 0;
    }

    delete[] (unsigned char*)PointIndices;
    delete[] (unsigned char*)Vertices;
    delete[] (unsigned char*)SkinPairs;
    delete[] (unsigned char*)SkinTable;
    delete[] (unsigned char*)UvIndices;
    delete[] (unsigned char*)UvData;
    delete[] (unsigned char*)UvDeform;

    if (BoneVerts != nullptr)
    {
        int* Header = (int*)BoneVerts - 1;
        int Count = *Header;
        int Index = Count - 1;
        while (Index >= 0)
        {
            DestroyBoneVertexList(&BoneVerts[Index]);
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
        BoneVerts = nullptr;
    }

    FVertexRemapList* RemapCursor = RemapStart;
    FVertexRemapList* RemapEnd = RemapFinish;
    while (RemapCursor != RemapEnd)
    {
        DestroyVertexRemapList(RemapCursor);
        RemapCursor += 1;
    }
    delete[] (unsigned char*)RemapStart;
    RemapStart = nullptr;
    RemapFinish = nullptr;
    RemapCapacity = nullptr;

    delete[] FaceFlags;
    FaceFlags = nullptr;
    PointIndices = nullptr;
    Vertices = nullptr;
    SkinPairs = nullptr;
    SkinTable = nullptr;
    UvIndices = nullptr;
    UvData = nullptr;
    UvDeform = nullptr;
}

// PTR_LAB_0660135c slot 0 (LAB_065f8b40) and slot 1 (LAB_065f8b60).
int FMeshArchetype::GetIndexCount()
{
    return MeshTotalIndexCount(this);
}

int FMeshArchetype::GetVertexCapacity()
{
    return MeshVertexCapacity(this);
}

// Slot 4 (LAB_065f8d00, RET 8).
unsigned char FMeshArchetype::GetBoundSphere(float* OutRadius, float* OutCenter)
{
    return ComputeMeshBoundSphere(this, OutRadius, OutCenter);
}

void FillFileDesc(FFileSystemDesc* Desc, const char* Path)
{
    std::memset(Desc, 0, sizeof(FFileSystemDesc));
    Desc->Size = 0x34;
    Desc->ComponentName = G_FileSystemName;
    Desc->Path = Path;
    Desc->Access = GENERIC_READ;
    Desc->ShareMode = FILE_SHARE_READ;
    Desc->Creation = OPEN_EXISTING;
    Desc->Flags = FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL;
    Desc->Field30 = 0xFFFFFFFF;
}

// Enumerate FileSystem children using the published ReadFile contract.
static int FsFindFirst(IFileSystem* Fs, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return Fs->FindFirst(Pattern,
                      FindData);
}

static int FsFindNext(IFileSystem* Fs, int Finder, WIN32_FIND_DATAA* FindData)
{
    return Fs->FindNext((unsigned long)Finder, FindData);
}

static void FsFindClose(IFileSystem* Fs, int Finder)
{
    Fs->FindClose((unsigned long)Finder);
}

// FUN_065f71c0: texcoord extra from FVF bits 8–11 count, sizes at bit 16+.
static int FvfTexcoordBytes(unsigned long Fvf)
{
    int Bytes = 0;
    unsigned int SetCount = (Fvf >> 8) & 0xF;
    unsigned int Bit = 0x10;
    while (SetCount != 0)
    {
        switch ((Fvf >> (Bit & 31)) & 3)
        {
        case 0:
            Bytes += 8;
            break;
        case 1:
            Bytes += 0xC;
            break;
        case 2:
            Bytes += 0x10;
            break;
        default:
            Bytes += 4;
            break;
        }
        Bit += 2;
        SetCount -= 1;
    }
    return Bytes;
}

// FUN_065f7220: D3DFVF_DIFFUSE (0x40) / D3DFVF_SPECULAR (0x80).
static int FvfColorBytes(unsigned long Fvf)
{
    int Bytes = 0;
    if ((Fvf & 0x40) != 0)
        Bytes += 4;
    if ((Fvf & 0x80) != 0)
        Bytes += 4;
    return Bytes;
}

// LAB_065f6bf0 FVF stride. ParseTag 0x112 → xyz 0xC + nrm 0xC + uv0 8 = 0x20.
static int ComputeFvfStride(unsigned long Fvf)
{
    int Position = 0;
    if ((Fvf & 0xC) == 0xC)
        Position = 0x1C;
    else if ((Fvf & 4) == 4)
        Position = 0x10;
    else if ((Fvf & 2) == 2)
        Position = 0xC;
    int Normal = ((Fvf & 0x10) != 0) ? 0xC : 0;
    return Position + Normal + FvfColorBytes(Fvf) + FvfTexcoordBytes(Fvf);
}

// FUN_065f96b0 call-site in FUN_065f89e0: insert 1 ushort at vector finish (push_back).
static void BoneListPush(FBoneVertexList* List, unsigned short VertexIndex)
{
    if (List == nullptr)
        return;
    int Used = 0;
    int Cap = 0;
    if (List->Start != nullptr && List->Finish != nullptr)
        Used = (int)(List->Finish - List->Start);
    if (List->Start != nullptr && List->CapacityEnd != nullptr)
        Cap = (int)(List->CapacityEnd - List->Start);
    if (Used >= Cap)
    {
        int NewCap = (Cap <= 0) ? 4 : Cap * 2;
        unsigned short* NewBuf = (unsigned short*)new (std::nothrow) unsigned char[(size_t)NewCap * sizeof(unsigned short)];
        if (NewBuf == nullptr)
            return;
        if (List->Start != nullptr && Used > 0)
            std::memcpy(NewBuf, List->Start, (size_t)Used * sizeof(unsigned short));
        if (List->Start != nullptr)
            delete[] (unsigned char*)List->Start;
        List->Start = NewBuf;
        List->Finish = NewBuf + Used;
        List->CapacityEnd = NewBuf + NewCap;
    }
    if (List->Finish == nullptr)
        return;
    *List->Finish = VertexIndex;
    List->Finish += 1;
}

// FUN_065f89e0 tail, 065f8a27..8ac2: a counted array of Holder+8 (the bound bone count)
static void BuildBoneVertexLists(FMeshArchetype* Arch, FLodHolder* Holder, IFileSystem* Fs)
{
    const int BoneCount = Holder->BindCursor;
    const unsigned char AllocatorByte = (unsigned char)((uintptr_t)Fs);
    unsigned char* Block = new unsigned char[(size_t)BoneCount * sizeof(FBoneVertexList) + sizeof(int)];
    *(int*)Block = BoneCount;
    FBoneVertexList* Lists = (FBoneVertexList*)(Block + sizeof(int));
    int ListIndex = 0;
    while (ListIndex < BoneCount)
    {
        Lists[ListIndex].Pad00 = AllocatorByte;
        Lists[ListIndex].Start = nullptr;
        Lists[ListIndex].Finish = nullptr;
        Lists[ListIndex].CapacityEnd = nullptr;
        ListIndex += 1;
    }
    Arch->BoneVerts = Lists;

    unsigned int Vertex = 0;
    while (Vertex < (unsigned int)Arch->VertexCount)
    {
        const FSkinVertex* Skin = &Arch->SkinTable[Vertex];
        int Index = 0;
        while (Index < Skin->BoneCount)
        {
            BoneListPush(&Lists[Skin->Pairs[Index].BoneId], (unsigned short)Vertex);
            Index += 1;
        }
        Vertex += 1;
    }
}

static void RemapListPush(FVertexRemapList* List, unsigned int PointIndex)
{
    if (List == nullptr)
        return;
    int Used = 0;
    int Cap = 0;
    if (List->Start != nullptr && List->Finish != nullptr)
        Used = (int)(List->Finish - List->Start);
    if (List->Start != nullptr && List->CapacityEnd != nullptr)
        Cap = (int)(List->CapacityEnd - List->Start);
    if (Used >= Cap)
    {
        int NewCap = (Cap <= 0) ? 4 : Cap * 2;
        unsigned int* NewBuf = (unsigned int*)new (std::nothrow) unsigned char[(size_t)NewCap * sizeof(unsigned int)];
        if (NewBuf == nullptr)
            return;
        if (List->Start != nullptr && Used > 0)
            std::memcpy(NewBuf, List->Start, (size_t)Used * sizeof(unsigned int));
        if (List->Start != nullptr)
            delete[] (unsigned char*)List->Start;
        List->Start = NewBuf;
        List->Finish = NewBuf + Used;
        List->CapacityEnd = NewBuf + NewCap;
    }
    if (List->Finish == nullptr)
        return;
    *List->Finish = PointIndex;
    List->Finish += 1;
}

// FUN_065f75f0: remap[PointIndices[i]].push(i). vector<uint> @ mesh+0x64.
static void BuildVertexRemap(FMeshArchetype* Arch)
{
    // FUN_065f75f0 (cdecl: mesh), 065f75f0..7757: +0x24 (VertexCount) == 0 -> return. The
    if (Arch->VertexCount == 0)
        return;

    const size_t VertexCount = (size_t)((unsigned int)Arch->VertexCount);
    FVertexRemapList* Lists =
        (FVertexRemapList*)new unsigned char[VertexCount * sizeof(FVertexRemapList)];
    std::memset(Lists, 0, VertexCount * sizeof(FVertexRemapList));
    Arch->RemapStart = Lists;
    Arch->RemapFinish = Lists + VertexCount;
    Arch->RemapCapacity = Arch->RemapFinish;

    const unsigned long* Indices = Arch->PointIndices;
    const unsigned int Count = (unsigned int)Arch->PointIndexCount;
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        const unsigned int Vertex = Indices[Index];
        if (Vertex < (unsigned int)Arch->VertexCount)
            RemapListPush(&Lists[Vertex], Index);
    }
}

// FUN_065f7590: if group+8 Material live, vtable+0x50 GetName, 12-byte cmpsb vs "EyeMaterial"
// → group+0x20 = 0. No Pad0C write. No vertex UV offset.
static void BindUvBoneIdsToFaceGroups(FMeshArchetype* Arch)
{
    // FUN_065f7590 (cdecl: mesh), 065f7590..75eb: for i < +0x18 (JLE on entry, JL loop, count
    // re-read): group+8 non-null -> slot 20 ClassName, REPE CMPSB 12 bytes vs "EyeMaterial"
    // (.data:06602790), equal -> group+0x20 = 0. AL = 1. No other tests.
    for (int Index = 0; Index < Arch->FaceGroupCount; ++Index)
    {
        FFaceGroup* Group = &Arch->FaceGroups[Index];
        if (Group->Material == nullptr)
            continue;
        // Slot 20 is __thiscall (render/IMaterial.h). The raw call here declared it
        const char* Name = Group->Material->ClassName();
        // strncmp over 12 == REPE CMPSB over 12 here: the 12th byte of "EyeMaterial" is its
        // NUL, so both stop at the same first difference.
        if (std::strncmp(Name, G_EyeMaterialName, 12) == 0)
            Group->UvBoneIndex = 0;
    }
}

// PTR_LAB_0660135c slot 0 LAB_065f8b40, 065f8b40..8b58: EAX = 0; +0x18 > 0 (JLE) -> add
// group +0x1C for each face group (+0x1C, stride 0x24). No tests of the mesh or the groups
// (until 2026-09-30 the port returned 0 for a null mesh or a null group array).
static int MeshTotalIndexCount(FMeshArchetype* Arch)
{
    int Total = 0;
    for (int Index = 0; Index < Arch->FaceGroupCount; ++Index)
        Total += (int)(Arch->FaceGroups[Index].IndexCount);
    return Total;
}

// PTR_LAB_0660135c slot 1 LAB_065f8b60: EAX = [this+0x20] + [this+0x10]. No test (until
// 2026-09-30 the port returned 0 for a null mesh, and added the skin pair count its own
// loader stored in +0x20 -- see Field_0x20).
static int MeshVertexCapacity(FMeshArchetype* Arch)
{
    return (int)Arch->Field_0x20 + Arch->PointIndexCount;
}

static void WriteVec3(float* Dest, float X, float Y, float Z);
static void ScaleVec3(float* Out, const float* In, float Scale);
static void SubVec3(float* Out, const float* A, const float* B);

// LAB_065f8d00, the DFM mesh's slot 4 (thiscall RET 8: OutRadius, OutCenter), 065f8d00..90b8:
static unsigned char ComputeMeshBoundSphere(FMeshArchetype* Arch, float* OutRadius, float* OutCenter)
{
    if (Arch->SphereReady == 0)
    {
        const unsigned int Count = (unsigned int)Arch->VertexCount;
        const float* const Points = (const float*)Arch->Vertices;
        Arch->SphereCenter[2] = 0.0f;
        Arch->SphereCenter[1] = 0.0f;
        Arch->SphereCenter[0] = 0.0f;
        Arch->SphereRadius = 0.0f;
        const unsigned char bNonEmpty = (Count != 0) ? 1 : 0;
        if (bNonEmpty != 0)
        {
            unsigned int MinX = 0;
            unsigned int MaxX = 0;
            unsigned int MinY = 0;
            unsigned int MaxY = 0;
            unsigned int MinZ = 0;
            unsigned int MaxZ = 0;
            unsigned int Index = 1;
            while (Index < Count)
            {
                const float* const P = Points + Index * 3;
                if ((double)P[0] < Points[MinX * 3])
                    MinX = Index;
                else if ((double)P[0] > Points[MaxX * 3])
                    MaxX = Index;
                if ((double)P[1] < Points[MinY * 3 + 1])
                    MinY = Index;
                else if ((double)P[1] > Points[MaxY * 3 + 1])
                    MaxY = Index;
                if ((double)P[2] < Points[MinZ * 3 + 2])
                    MinZ = Index;
                else if ((double)P[2] > Points[MaxZ * 3 + 2])
                    MaxZ = Index;
                Index += 1;
            }
            const float Dx = Points[MaxX * 3] - Points[MinX * 3];
            const float Dy = Points[MaxY * 3 + 1] - Points[MinY * 3 + 1];
            const float Dz = Points[MaxZ * 3 + 2] - Points[MinZ * 3 + 2];
            float Sum[3];
            Sum[0] = Points[MinX * 3] + Points[MaxX * 3];
            Sum[1] = Points[MaxY * 3 + 1] + Points[MinY * 3 + 1];
            Sum[2] = Points[MaxZ * 3 + 2] + Points[MinZ * 3 + 2];
            float Center[3];
            ScaleVec3(Center, Sum, 0.5f);
            Arch->SphereCenter[0] = Center[0];
            Arch->SphereCenter[1] = Center[1];
            Arch->SphereCenter[2] = Center[2];
            float Extent = (Dx > Dy) ? Dx : Dy;
            if (!(Extent > Dz))
                Extent = Dz;
            const double Radius = (double)Extent * 0.5;
            Arch->SphereRadius = (float)Radius;
            float RadiusSq = (float)(Radius * Radius);

            Index = 0;
            while (Index < Count)
            {
                const float* const P = Points + Index * 3;
                float D[3];
                SubVec3(D, P, Arch->SphereCenter);
                const double DistSq = ((double)D[1] * D[1] + (double)D[2] * D[2]) +
                                      (double)D[0] * D[0];
                if (DistSq > (double)RadiusSq)
                {
                    const double Dist = std::sqrt(DistSq);
                    const float DistF = (float)Dist;
                    const double NewRadius =
                        (Dist + (double)Arch->SphereRadius) * 0.5;
                    Arch->SphereRadius = (float)NewRadius;
                    RadiusSq = (float)(NewRadius * NewRadius);
                    const float Shift = (float)((double)DistF - NewRadius);
                    float Toward[3];
                    ScaleVec3(Toward, P, Shift);
                    float Kept[3];
                    ScaleVec3(Kept, Arch->SphereCenter, Arch->SphereRadius);
                    float Blend[3];
                    WriteVec3(Blend, Toward[0] + Kept[0], Toward[1] + Kept[1], Toward[2] + Kept[2]);
                    const double Inv = 1.0 / (double)DistF;
                    float NewCenter[3];
                    WriteVec3(NewCenter, (float)((double)Blend[0] * Inv),
                              (float)((double)Blend[1] * Inv),
                              (float)((double)Blend[2] * Inv));
                    Arch->SphereCenter[0] = NewCenter[0];
                    Arch->SphereCenter[1] = NewCenter[1];
                    Arch->SphereCenter[2] = NewCenter[2];
                }
                Index += 1;
            }
        }
        Arch->SphereReady = bNonEmpty;
    }
    OutCenter[0] = Arch->SphereCenter[0];
    OutCenter[1] = Arch->SphereCenter[1];
    OutCenter[2] = Arch->SphereCenter[2];
    *OutRadius = Arch->SphereRadius;
    return 1;
}

// FUN_065f6ad0: out[i] = weight * matrix[i] (12 floats).
static void ScaleBoneMatrix(float* Out, const float* Matrix, float Weight)
{
    int Index = 0;
    while (Index < 12)
    {
        Out[Index] = Weight * Matrix[Index];
        Index += 1;
    }
}

// FUN_065f6b00: out[i] += weight * matrix[i].
// FUN_065f6b00: 12 x (FLD Weight, FMUL m[i], FADD out[i], FSTP float out[i]) -- one rounding.
static void AccumulateBoneMatrix(float* Out, const float* Matrix, float Weight)
{
    int Index = 0;
    while (Index < 12)
    {
        Out[Index] = (float)((double)Weight * Matrix[Index] +
                                        (double)Out[Index]);
        Index += 1;
    }
}

// FUN_065f6b30: out = M3x4 * point (translation m[9..11]).
static void TransformPoint(float* Out, const float* Matrix, const float* Point)
{
    // Point is re-read per row, as retail does (matters only if Out aliases it).
    Out[0] = (float)((((double)Matrix[2] * Point[2] +
                                  (double)Matrix[1] * Point[1]) +
                                 (double)Matrix[0] * Point[0]) +
                                Matrix[9]);
    Out[1] = (float)((((double)Matrix[5] * Point[2] +
                                  (double)Matrix[3] * Point[0]) +
                                 (double)Matrix[4] * Point[1]) +
                                Matrix[10]);
    Out[2] = (float)((((double)Matrix[8] * Point[2] +
                                  (double)Matrix[6] * Point[0]) +
                                 (double)Matrix[7] * Point[1]) +
                                Matrix[11]);
}

// FUN_065f6b90: out = M3x3 * direction (no translation).
// FUN_065f6b90 (cdecl: Out, M, D), 065f6b90..6bec: as 6b30 without the translation terms,
// same summation order.
static void TransformDirection(float* Out, const float* Matrix, const float* Direction)
{
    Out[0] = (float)(((double)Matrix[2] * Direction[2] +
                                 (double)Matrix[1] * Direction[1]) +
                                (double)Matrix[0] * Direction[0]);
    Out[1] = (float)(((double)Matrix[5] * Direction[2] +
                                 (double)Matrix[3] * Direction[0]) +
                                (double)Matrix[4] * Direction[1]);
    Out[2] = (float)(((double)Matrix[8] * Direction[2] +
                                 (double)Matrix[6] * Direction[0]) +
                                (double)Matrix[7] * Direction[1]);
}

// FUN_065f70b0 thiscall RET 4: dest = -this[0..2] (FCHS stack 2,1,0).
static void NegateVec3(const float* Source, float* Dest)
{
    Dest[0] = -Source[0];
    Dest[1] = -Source[1];
    Dest[2] = -Source[2];
}

// FUN_065f70d0 thiscall RET 4: 3x3 transpose into 9 floats (m0,m3,m6 / m1,m4,m7 / m2,m5,m8).
// FUN_065f70d0: every element is loaded before the first store (FPU dump), so an in-place
// transpose works; the copy below keeps that.
static void Transpose3x3(const float* Source, float* Dest)
{
    float Copy[9];
    std::memcpy(Copy, Source, sizeof(Copy));
    Dest[0] = Copy[0];
    Dest[1] = Copy[3];
    Dest[2] = Copy[6];
    Dest[3] = Copy[1];
    Dest[4] = Copy[4];
    Dest[5] = Copy[7];
    Dest[6] = Copy[2];
    Dest[7] = Copy[5];
    Dest[8] = Copy[8];
}

// FUN_065f90c0 listing 17907–17959: stosd 0x1C dwords, type=6, xyz 0xC/0x18, nrm=verts+0xC, UV0 if +0x50.
static void FillMeshStreamDesc(FMeshArchetype* This, FRpVertexSrcDesc* Out)
{
    // FUN_065f90c0: STOSD 0x1C zero dwords, then the fields below. No tests.
    std::memset(Out, 0, sizeof(FRpVertexSrcDesc));
    Out->SrcFvf = This->ParseTag;
    Out->Flags = 6;
    Out->SrcCount = This->PointIndexCount;
    Out->Position.Base = (const unsigned char*)This->Vertices;
    Out->Position.ByteSize = 0xC;
    Out->Position.Stride = 0x18;
    Out->Position.IndexArray = This->PointIndices;
    Out->Position.Count = This->VertexCount;
    Out->Normal.Base = (unsigned char*)This->Vertices + 0xC;
    Out->Normal.ByteSize = 0xC;
    Out->Normal.Stride = 0x18;
    Out->Normal.IndexArray = This->PointIndices;
    Out->Normal.Count = This->VertexCount;
    if (This->UvData != nullptr)
    {
        Out->Tex[0].Base = (const unsigned char*)This->UvData;
        Out->Tex[0].ByteSize = 8;
        Out->Tex[0].Stride = 8;
        Out->Tex[0].IndexArray = This->UvIndices;
        Out->Tex[0].Count = This->UvCount;
    }
}

static void WriteIdentity3x3(float* Matrix);
static void ComposeBoneWorldMatrix(FBoneRuntime* Slot, const float* InverseRoot);
static I3DMathEngine* AcquireMathEngine();

// Reuse a valid VB or recreate it at the descriptor capacity.
static unsigned char EnsurePartVertexBuffer(FDeformVbDesc* Vb, IRPVertexBuffer* VertexBuffer,
                                            unsigned long Fvf, int /*Count*/)
{
    Vb->Listing.Fvf = Fvf;
    Vb->Listing.BaseVertex = 0;
    if (VertexBuffer->IsVbValid(Vb->Listing.VertexBuffer) != 0)
        return 1;
    VertexBuffer->DestroyVb(&Vb->Listing.VertexBuffer);
    return VertexBuffer->CreateVb((unsigned int)(Vb->Listing.Fvf),
                                  (unsigned int)Vb->Capacity,
                                  (unsigned int**)(&Vb->Listing.VertexBuffer),
                                  0) >= 0
               ? 1
               : 0;
}

// LAB_065f6bf0, the DFM mesh's slot 3 (thiscall RET 0xC: Holder, &part+0x14, bForce),
unsigned char FMeshArchetype::Skin(FLodHolder* Holder, FDeformVbDesc* Vb, unsigned char bForce)
{
    const float* const Root =
        Holder->BoneSlots[0].LocalTransform;
    float RootPos[3];
    MathTransformVec3(RootPos, Root, Root + 9);
    float InverseRoot[12];
    NegateVec3(RootPos, InverseRoot + 9);
    Transpose3x3(Root, InverseRoot);

    SkinnedVertexCount = 0;
    SkinnedBoneCount = 0;
    int BoneIndex = 0;
    while (BoneIndex < Holder->BindCursor)
    {
        FBoneRuntime* const Bone = &Holder->BoneSlots[BoneIndex];
        const FBoneVertexList* const List = &BoneVerts[BoneIndex];
        const unsigned char BoneFlags = Bone->Flags;
        if ((BoneFlags & 2) != 0 || bForce != 0)
        {
            Bone->Flags = (unsigned char)(BoneFlags & 0xFD);
            if (List->Start != nullptr && (List->Finish - List->Start) != 0)
            {
                SkinnedBoneCount += 1;
                if (Flag70 == 0)
                {
                    WriteIdentity3x3(Bone->Matrix);
                    Bone->Matrix[11] = 0.0f;
                    Bone->Matrix[10] = 0.0f;
                    Bone->Matrix[9] = 0.0f;
                }
                else
                {
                    ComposeBoneWorldMatrix(Bone, InverseRoot);
                    unsigned char* const Dirty = FaceFlags;
                    const unsigned short* Cursor = List->Start;
                    while (List->Finish != Cursor)
                    {
                        Dirty[*Cursor] = 1;
                        Cursor += 1;
                    }
                }
            }
        }
        BoneIndex += 1;
    }

    const int Stride = ComputeFvfStride(ParseTag);
    const FVertexRemapList* const Remap = RemapStart;
    unsigned char* Locked = nullptr;
    const int Capacity = GetVertexCapacity();
    IRPVertexBuffer* const VertexBuffer = G_VertexBuffer;
    if (EnsurePartVertexBuffer(Vb, VertexBuffer, ParseTag, Capacity) == 0)
        return 0;
    Vb->Listing.BaseVertex = 0;
    if (VertexBuffer->LockVb(Vb->Listing.VertexBuffer,
                             (unsigned int*)(&Vb->Listing.BaseVertex),
                             (void**)(&Locked),
                             (unsigned int)Capacity) < 0)
        return 0;

    if (bForce != 0)
    {
        FRpVertexSrcDesc StreamDesc;
        FillMeshStreamDesc(this, &StreamDesc);
        if (G_VertexBuffer->CopyVertices(
                Vb->Listing.VertexBuffer, (unsigned int*)&Vb->Listing.BaseVertex, &StreamDesc, 0,
                (unsigned int)PointIndexCount) < 0)
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformablePart, 0x134,
                  "failed to copy deformable verts during update_verts");
            G_VertexBuffer->UnlockVb(Vb->Listing.VertexBuffer);
            return 0;
        }
    }

    unsigned char* const Dirty = FaceFlags;
    const unsigned char* const Positions = (const unsigned char*)Vertices;
    unsigned int Vertex = 0;
    while (Vertex < (unsigned int)VertexCount)
    {
        if (Dirty[Vertex] != 0)
        {
            Dirty[Vertex] = 0;
            SkinnedVertexCount += 1;
            const FSkinVertex* const SkinVertex = &SkinTable[Vertex];
            const FSkinPair* const Pairs = SkinVertex->Pairs;
            const float* const Source =
                (const float*)(Positions + (size_t)Vertex * 0x18);
            const float* const First = Holder->BoneSlots[Pairs[0].BoneId].Matrix;
            float Out[6];
            const int Count = SkinVertex->BoneCount;
            if (Count == 1)
            {
                TransformPoint(Out, First, Source);
                TransformDirection(Out + 3, First, Source + 3);
            }
            else
            {
                float Blend[12];
                ScaleBoneMatrix(Blend, First, Pairs[0].Weight);
                if (Count == 4)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[3].BoneId].Matrix,
                                         Pairs[3].Weight);
                if (Count == 4 || Count == 3)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[2].BoneId].Matrix,
                                         Pairs[2].Weight);
                if (Count == 4 || Count == 3 || Count == 2)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[1].BoneId].Matrix,
                                         Pairs[1].Weight);
                TransformPoint(Out, Blend, Source);
                TransformDirection(Out + 3, Blend, Source + 3);
                const float LenSq = (float)(
                    ((double)Out[4] * Out[4] + (double)Out[5] * Out[5]) +
                    (double)Out[3] * Out[3]);
                int Bits = 0;
                std::memcpy(&Bits, &LenSq, sizeof(Bits));
                Bits = (int)(0x5F3759DFu - (unsigned int)(Bits >> 1));
                float Estimate = 0.0f;
                std::memcpy(&Estimate, &Bits, sizeof(Estimate));
                const double Y0 = (double)Estimate;
                const double InvLen =
                    (1.5 - Y0 * Y0 * (double)LenSq * 0.5) * Y0;
                Out[3] = (float)((double)Out[3] * InvLen);
                Out[4] = (float)((double)Out[4] * InvLen);
                Out[5] = (float)((double)Out[5] * InvLen);
            }
            const unsigned int* Cursor = Remap[Vertex].Start;
            const unsigned int* const End = Remap[Vertex].Finish;
            while (Cursor != End)
            {
                std::memcpy(Locked + (size_t)(*Cursor) * (size_t)Stride,
                            Out, 6 * sizeof(float));
                Cursor += 1;
            }
        }
        Vertex += 1;
    }
    G_VertexBuffer->UnlockVb(Vb->Listing.VertexBuffer);
    return 1;
}

// FUN_065f7340 thiscall face-group, RET 8: context, accumulated IB start.
static int DrawFaceGroup(FFaceGroup* Group, FMaterialSubmitContext* Context, int StartIndex)
{
    // 065f7340..739a: +0x1C == 0 (JBE) -> 0. NumVerts = +0x14 - +0x10 + 1; ctx+0x20 = +0xC;
    // Material (+8) +0x20 (ctx), +0x28 (ctx, Kind, IndexMin, NumVerts, Start, IndexCount,
    // 0x100), +0x24 (ctx); EAX = NumVerts. No tests of the context or the material.
    if (Group->IndexCount == 0)
        return 0;
    const int NumVerts =
        (int)Group->IndexMax - (int)Group->IndexMin + 1;
    Context->Field20 = Group->UvBinding;
    IMaterial* Material = Group->Material;
    Material->Bind(Context);
    Material->Submit(Context, Group->Kind, Group->IndexMin, (unsigned long)NumVerts,
                     (unsigned long)StartIndex, Group->IndexCount, 0x100ul);
    Material->Teardown(Context);
    return NumVerts;
}

// LAB_065f73a0, the DFM mesh's slot 5 (thiscall RET 8: Camera, Part), 065f73a0..758d. Frame
unsigned char FMeshArchetype::Draw(ICamera* Camera, FDeformPart* Part)
{
    FBoneRuntime* const BoneSlots = Part->Lod->BoneSlots;
    const float* const Root =
        BoneSlots[0].LocalTransform;
    const float* const View = Camera->GetViewTransform();
    int bHaveView = 1;
    G_Pipeline->GetPipelineState(7, &bHaveView);
    if (bHaveView != 0)
    {
        float Composed[12];
        AcquireMathEngine()->MulTransform(*(FTransform*)Composed,
                                          *(const FTransform*)View,
                                          *(const FTransform*)Root); // +0x1C
        float Modelview[12];
        std::memcpy(Modelview, Composed, sizeof(Modelview));
        G_Pipeline->SetModelview(Modelview);
    }
    else
    {
        float Identity[12];
        Identity[0] = 1.0f;
        Identity[1] = 0.0f;
        Identity[2] = 0.0f;
        Identity[3] = 0.0f;
        Identity[4] = 1.0f;
        Identity[5] = 0.0f;
        Identity[6] = 0.0f;
        Identity[7] = 0.0f;
        Identity[8] = 1.0f;
        Identity[9] = 0.0f;
        Identity[10] = 0.0f;
        Identity[11] = 0.0f;
        G_Pipeline->SetWorld(Identity);
    }

    FMaterialSubmitContext Context;
    Context.Inst = BoneSlots[0].Instance;
    // The mutable context ABI borrows these const transform views.
    Context.World = const_cast<float*>(Root);
    Context.View = const_cast<float*>(View);
    Context.SourceVertices = nullptr;
    Context.Indices = nullptr;
    Context.VbListing = &Part->Vb.Listing;
    Context.IbScratch = &Part->Ib.Scratch;
    // Retail leaves +0x1C as stack garbage; the port cannot reproduce that and stores 0.
    Context.ReplacedMaterial = nullptr;
    Context.Field20 = nullptr;

    FUvDeformBinding Binding;
    Binding.Record = nullptr;
    Binding.Bone = nullptr;
    Binding.EyeUvTransformCount = 0;
    unsigned long Start = 0;
    int GroupIndex = 0;
    while (GroupIndex < FaceGroupCount)
    {
        FFaceGroup* const Group = &FaceGroups[GroupIndex];
        if (UvDeformCount != 0 && (long)Group->UvBoneIndex >= 0)
        {
            FUvDeformRecord* const Record = &UvDeform[Group->UvBoneIndex];
            Binding.Record = Record;
            Binding.Bone = &Part->Lod->BoneSlots[Record->BoneId];
            Group->UvBinding = &Binding;
        }
        DrawFaceGroup(Group, &Context, (int)Start);
        Start += Group->IndexCount;
        GroupIndex += 1;
    }
    EyeUvTransformCount = Binding.EyeUvTransformCount;
    return 1;
}

// LAB_065f7240, the DFM mesh's slot 2 (thiscall RET 4: &part+0x24), 065f7240..732e.
unsigned char FMeshArchetype::FillIndexBuffer(FDeformIbDesc* Ib)
{
    const int IndexCount = GetIndexCount();
    IRPIndexBuffer* const IndexBuffer = G_IndexBuffer;
    unsigned short* Locked = nullptr;
    if (IndexBuffer->IsIbValid(Ib->Scratch.IndexBuffer) == 0)
    {
        IndexBuffer->DestroyIb(&Ib->Scratch.IndexBuffer);
        if (IndexBuffer->CreateIb((unsigned int)Ib->Capacity,
                                  (unsigned int**)(&Ib->Scratch.IndexBuffer),
                                  0) < 0)
            return 0;
    }
    Ib->Scratch.Bias = 0;
    if (IndexBuffer->LockIb(Ib->Scratch.IndexBuffer,
                            (unsigned int*)(&Ib->Scratch.Bias),
                            (void**)(&Locked),
                            (unsigned int)IndexCount) < 0)
        return 0;
    int GroupIndex = 0;
    while (GroupIndex < FaceGroupCount)
    {
        const FFaceGroup* const Group = &FaceGroups[GroupIndex];
        unsigned long Index = 0;
        while (Index < Group->IndexCount)
        {
            *Locked = (unsigned short)(Group->Indices[Index] +
                                                  (unsigned short)Group->IndexMin);
            Locked += 1;
            Index += 1;
        }
        GroupIndex += 1;
    }
    G_IndexBuffer->UnlockIb(Ib->Scratch.IndexBuffer);
    return 1;
}

static int ReleaseLodHolder(FLodHolder* Holder);

// FUN_065f6760 (thiscall), the part dtor, 065f6760..67ea.
void FDeformPart::Reset() // formerly DestroyPart
{
    // 065f6761..678e: IRPVertexBuffer +0x10 DestroyVb(&part+0x14), part+0x20 = 0;
    // the interfaces and freed the CPU copies of its scaffold (PreparePartGpuLayout).
    G_VertexBuffer->DestroyVb(&Vb.Listing.VertexBuffer);
    Vb.Capacity = 0;
    G_IndexBuffer->DestroyIb(&Ib.Scratch.IndexBuffer);
    Ib.Capacity = 0;

    // 065f6791: FUN_065f17e0 on +0 with no null test.
    ReleaseLodHolder(Lod);
    // 065f6798..67d2: the counted mesh array, last to first; each live entry through
    // FUN_065f28f0 and zeroed, then the block (header at -4).
    if (Meshes != nullptr)
    {
        int* Header = (int*)Meshes - 1;
        int Index = *Header - 1;
        while (Index >= 0)
        {
            if (Meshes[Index] != nullptr)
            {
                ReleaseDeformMesh(Meshes[Index]);
                Meshes[Index] = nullptr;
            }
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
    }
    // 065f67d6..67e1: +0 is released a second time when still set, then zeroed. The holder
    // carries two references for the part -- FUN_065f36c0 creates it at 1 and FUN_065f67f0
    // AddRefs it -- so this is the second of the pair, not a double free.
    if (Lod != nullptr)
    {
        ReleaseLodHolder(Lod);
        Lod = nullptr;
    }
}

// FUN_065f7c70 (thiscall, 065f7c70..7c96): +0 = 4, +4..+0x1C = 0, +0x20 = -1. No tests.
static void ConstructFaceGroup(FFaceGroup* Group)
{
    Group->Kind = 4;
    Group->MaterialCrc = 0;
    Group->Material = nullptr;
    Group->UvBinding = nullptr;
    Group->IndexMin = 0;
    Group->IndexMax = 0;
    Group->Indices = nullptr;
    Group->IndexCount = 0;
    Group->UvBoneIndex = 0xFFFFFFFF;
}

// FUN_065f7d70: tristrip shorts → triangle list. Not skin.
static void ExpandTristrip(const unsigned short* Strip, int StripCount, unsigned short* Out,
                           int* OutCount)
{
    // FUN_065f7d70 (cdecl), 065f7d70..7dfb: *OutCount = 0; Limit = Count - 2 tested unsigned
    // (JBE: only 0 skips -- a Count below 2 wraps, as in retail); no pointer tests.
    *OutCount = 0;
    const unsigned int Limit = (unsigned int)(StripCount - 2);
    if (Limit == 0)
        return;
    unsigned int Walk = 0;
    const unsigned short* Cursor = Strip + 2;
    do
    {
        const unsigned short A = Cursor[-2];
        const unsigned short B = Cursor[-1];
        const unsigned short C = Cursor[0];
        if (A != B && B != C && C != A)
        {
            Out[*OutCount] = A;
            if ((Walk & 1) == 0)
            {
                Out[*OutCount + 1] = B;
                Out[*OutCount + 2] = C;
            }
            else
            {
                Out[*OutCount + 1] = C;
                Out[*OutCount + 2] = B;
            }
            *OutCount += 3;
        }
        Walk += 1;
        Cursor += 1;
    } while (Walk < (unsigned int)(StripCount - 2));
}

// FUN_065f7e00: min/max + rebase. Index table wiring, not skin.
static void RebaseFaceIndices(FFaceGroup* Group)
{
    unsigned short* Indices = Group->Indices;
    const unsigned long Count = Group->IndexCount;
    const unsigned short Last = Indices[Count - 1];
    const unsigned short First = Indices[0];
    Group->IndexMin = (First < Last) ? First : Last;
    Group->IndexMax = (First >= Last) ? First : Last;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        const unsigned long Value = Indices[Index];
        if (Value < Group->IndexMin)
            Group->IndexMin = Value;
        else if (Value > Group->IndexMax)
            Group->IndexMax = Value;
    }
    const unsigned short Base = (unsigned short)Group->IndexMin;
    for (unsigned long Index = 0; Index < Count; ++Index)
        Indices[Index] = (unsigned short)(Indices[Index] - Base);
}

// FUN_065f8b00 (thiscall RET 4) on the calloc(1, 0x84) block of FUN_065f26f0: +4 = 1,
FMeshArchetype::FMeshArchetype(const char* Name)
    : FDeformMesh(Name)
{
    std::memset(&ParseTag, 0, sizeof(FMeshArchetype) - offsetof(FMeshArchetype, ParseTag));
    NameHint = (unsigned char)((uintptr_t)Name);
}

// The 0x14-byte mapped node FUN_065f7760 keeps on its stack twice (Material_name at
// [ESP+0x20], Tristrip_indices at [ESP+0x3c]): an open handle, its size, a file mapping and
// a view of it, and the FileSystem they came from, AddRef'd (FUN_065f9150).
struct FMappedFileNode
{
    int Handle;         // +0x00 IFileSystem +0x7C, -1 none
    unsigned long Size; // +0x04 IFileSystem +0x28
    int Mapping;        // +0x08 IFileSystem +0x3C (h, 0, PAGE_READONLY, 0, 0, 0), -1 none
    void* View;         // +0x0C IFileSystem +0x40 (mapping, FILE_MAP_READ, 0, 0, 0)
    IFileSystem* Fs;    // +0x10
};

// FUN_065f9150 (thiscall RET 4): +0 = the pointer, AddRef when non-null.
static void HoldFileSystem(IFileSystem** Slot, IFileSystem* Fs)
{
    *Slot = Fs;
    if (Fs != nullptr)
        Fs->AddRef();
}

// FUN_065f9170 (thiscall): Release a non-null +0 and zero it.
static void DropFileSystem(IFileSystem** Slot)
{
    if (*Slot != nullptr)
    {
        (*Slot)->Release();
        *Slot = nullptr;
    }
}

// FUN_065f7d10 / FUN_065f7ca0 (thiscall, the node's dtor -- 7ca0 repeats the final release,
// a no-op after the first): Handle != -1 -> (Mapping != -1 -> (View -> Fs +0x44 (View), View =
// 0); Fs +0x10 (Mapping)); Fs +0x10 (Handle), Handle = -1. Then FUN_065f9170 on Fs.
static void CloseMappedFileNode(FMappedFileNode* Node)
{
    if (Node->Handle != -1)
    {
        if (Node->Mapping != -1)
        {
            if (Node->View != nullptr)
            {
                Node->Fs->UnmapFileView((unsigned long)((uintptr_t)Node->View));
                Node->View = nullptr;
            }
            Node->Fs->CloseFile((unsigned long)Node->Mapping);
        }
        Node->Fs->CloseFile((unsigned long)Node->Handle);
        Node->Handle = -1;
    }
    DropFileSystem(&Node->Fs);
}

// The open-and-map sequence FUN_065f7760 inlines twice (065f795a..79bc, 065f7aba..7b18):
// +0x7C on the node's FileSystem; a handle -> Size = +0x28 (h, 0), Mapping = +0x3C (h, 0, 2, 0,
// 0, 0); a mapping -> View = +0x40 (Mapping, 4, 0, 0, 0).
static void OpenMappedFileNode(FMappedFileNode* Node, const char* NodeName)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, NodeName);
    Node->Handle = Node->Fs->OpenByDesc(&Desc);
    if (Node->Handle == -1)
        return;
    Node->Size = Node->Fs->GetSize((unsigned long)Node->Handle, 0);
    Node->Mapping = Node->Fs->CreateMapping((unsigned long)Node->Handle, 0, 2, 0, 0, 0);
    if (Node->Mapping == -1)
        return;
    Node->View = (void*)((uintptr_t)((unsigned int)(
        Node->Fs->MapFileView((unsigned long)Node->Mapping, 4, 0, 0, 0))));
}

// FUN_065f8360 (thiscall RET 4: Fs): +0x10 = Fs, AddRef when non-null; Handle = -1, Size = 0,
// View = 0. Mapping is not touched (every reader tests Handle first).
static void InitMappedFileNode(FMappedFileNode* Node, IFileSystem* Fs)
{
    HoldFileSystem(&Node->Fs, Fs);
    Node->Handle = -1;
    Node->Size = 0;
    Node->View = nullptr;
}

// FUN_065f8390 (thiscall RET 8: name, find handle), 065f8390..846d: the 0x34 desc of
static int MapFileNode(FMappedFileNode* Node, const char* NodeName, unsigned long FindHandle)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, NodeName);
    Desc.Field30 = FindHandle;
    Node->Handle = Node->Fs->OpenByDesc(&Desc);
    if (Node->Handle == -1)
        return -8;
    Node->Size = Node->Fs->GetSize((unsigned long)Node->Handle, 0);
    Node->Mapping = Node->Fs->CreateMapping((unsigned long)Node->Handle, 0, 2, 0, 0, 0);
    if (Node->Mapping == -1)
        return -6;
    Node->View = (void*)((uintptr_t)((unsigned int)(
        Node->Fs->MapFileView((unsigned long)Node->Mapping, 4, 0, 0, 0))));
    if (Node->View != nullptr)
        return 0;
    return -1;
}

// FUN_065f7760 (thiscall on the mesh, RET 4: Fs), 065f7760..7c68, 0x425c-byte frame.
static bool LoadFaceGroups(FMeshArchetype* Arch, IFileSystem* Fs)
{
    if (Fs->OpenNode("Face_groups") == 0)
        return false;

    int Count = 0;
    FFileSystemDesc CountDesc;
    FillFileDesc(&CountDesc, "Count");
    const int CountHandle = Fs->OpenByDesc(&CountDesc);
    if (CountHandle != -1)
    {
        int Value = 0;
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)CountHandle, &Value, 4, &Got, 0) != 0 && Got == 4)
            Count = Value;
        Fs->CloseFile((unsigned long)CountHandle);
    }
    int* Block = (int*)new unsigned char[(size_t)Count * sizeof(FFaceGroup) + 4];
    *Block = Count;
    FFaceGroup* Groups = (FFaceGroup*)(Block + 1);
    int GroupIndex = 0;
    while (GroupIndex < Count)
    {
        ConstructFaceGroup(&Groups[GroupIndex]);
        GroupIndex += 1;
    }
    Arch->FaceGroups = Groups;

    WIN32_FIND_DATAA FindData;
    const int Finder = FsFindFirst(Fs, "Group*", &FindData);
    if (Finder != -1)
    {
        for (;;)
        {
            FFaceGroup* Group = &Arch->FaceGroups[Arch->FaceGroupCount];
            IFileSystem* Child = nullptr;
            DropFileSystem(&Child);
            FFileSystemDesc ChildDesc;
            FillFileDesc(&ChildDesc, FindData.cFileName);
            ChildDesc.Field30 = (unsigned long)Finder;
            if (Fs->CreateInstance((AGGDESC*)(&ChildDesc),
                                   (void**)(&Child)) < 0)
            {
                char Message[0x2000];
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformablePartArchetype,
                      0x37, FormatDeformMessage(Message, "couldnt open dfm face group"));
                DropFileSystem(&Child);
            }
            else
            {
                FMappedFileNode MaterialNode;
                HoldFileSystem(&MaterialNode.Fs, Child);
                MaterialNode.Handle = -1;
                MaterialNode.Size = 0;
                MaterialNode.View = nullptr;
                OpenMappedFileNode(&MaterialNode, "Material_name");
                const char* MaterialName = (const char*)MaterialNode.View;
                if (MaterialName == nullptr)
                {
                    CloseMappedFileNode(&MaterialNode);
                    DropFileSystem(&MaterialNode.Fs);
                    DropFileSystem(&Child);
                }
                else
                {
                    Group->MaterialCrc = DACOM_CRC::GetCRC32(MaterialName);
                    FEngineName Query;
                    Query.Text = MaterialName;
                    Query.Crc = DACOM_CRC::GetCRC32(MaterialName);
                    if (G_MaterialLibrary
                            ->FindMaterialByCrc(&Query, &Group->Material) < 0)
                    {
                        char Message[0x2000];
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              G_FdumpFileDeformablePartArchetype, 0x45,
                              FormatDeformMessage(Message, "couldnt find material %s for %s", "???",
                                                  ""));
                        CloseMappedFileNode(&MaterialNode);
                        DropFileSystem(&MaterialNode.Fs);
                        DropFileSystem(&Child);
                    }
                    else
                    {
                        FMappedFileNode IndexNode;
                        HoldFileSystem(&IndexNode.Fs, Child);
                        IndexNode.Handle = -1;
                        IndexNode.Size = 0;
                        IndexNode.View = nullptr;
                        OpenMappedFileNode(&IndexNode, "Tristrip_indices");
                        if (IndexNode.Size != 0)
                        {
                            const int StripCount = (int)(IndexNode.Size >> 1);
                            Group->Indices = (unsigned short*)new unsigned char[(size_t)(StripCount * 6 - 12)];
                            ExpandTristrip((const unsigned short*)IndexNode.View,
                                           StripCount, Group->Indices,
                                           (int*)(&Group->IndexCount));
                        }
                        else
                        {
                            FFileSystemDesc FaceDesc;
                            FillFileDesc(&FaceDesc, "Face_indices");
                            const int FaceHandle = Child->OpenByDesc(&FaceDesc);
                            if (FaceHandle != -1)
                            {
                                const unsigned long FaceBytes =
                                    Child->GetSize((unsigned long)FaceHandle, 0);
                                const unsigned long FaceCount = FaceBytes >> 1;
                                unsigned short* Buffer = (unsigned short*)new unsigned char[(size_t)FaceCount * 2];
                                unsigned long Got = 0;
                                if (Child->Read((unsigned long)FaceHandle, Buffer,
                                                FaceBytes, &Got, 0) != 0)
                                {
                                    Group->IndexCount = FaceCount;
                                    Group->Indices = Buffer;
                                }
                                else
                                {
                                    delete[] (unsigned char*)Buffer;
                                }
                                Child->CloseFile((unsigned long)FaceHandle);
                            }
                        }
                        Group->Kind = 4;
                        RebaseFaceIndices(Group);
                        CloseMappedFileNode(&IndexNode);
                        CloseMappedFileNode(&MaterialNode);
                        DropFileSystem(&Child);
                    }
                }
            }

            Arch->FaceGroupCount += 1;
            if (Arch->FaceGroupCount >= Count)
                break;
            if (FsFindNext(Fs, Finder, &FindData) == 0)
                break;
        }
        FsFindClose(Fs, Finder);
    }
    Fs->OpenNode(G_ParentNode); // 065f7c53
    return true;
}

// FUN_065f7e80 (thiscall on the mesh, RET 4: Fs), 065f7e80..835b, 0x7c-byte frame.
static bool LoadGeometry(FMeshArchetype* Arch, IFileSystem* Fs)
{
    if (Fs->OpenNode("Geometry") == 0)
        return false;

    FFileSystemDesc Desc;
    FillFileDesc(&Desc, "Point_indices");
    const int IndexHandle = Fs->OpenByDesc(&Desc);
    if (IndexHandle != -1)
    {
        const unsigned long IndexBytes = Fs->GetSize((unsigned long)IndexHandle, 0);
        const unsigned long IndexCount = IndexBytes >> 2;
        unsigned char* Indices = new unsigned char[(size_t)IndexCount * 4];
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)IndexHandle, Indices, IndexBytes, &Got, 0) != 0)
        {
            Arch->PointIndexCount = (int)IndexCount;
            Arch->PointIndices = (unsigned long*)Indices;
        }
        else
        {
            delete[] Indices;
        }
        Fs->CloseFile((unsigned long)IndexHandle);
    }

    FMappedFileNode PointNode;
    HoldFileSystem(&PointNode.Fs, Fs);
    PointNode.Handle = -1;
    PointNode.Size = 0;
    PointNode.View = nullptr;
    OpenMappedFileNode(&PointNode, "Points");
    const unsigned long* PointView = (const unsigned long*)PointNode.View;

    FMappedFileNode NormalNode;
    HoldFileSystem(&NormalNode.Fs, Fs);
    NormalNode.Handle = -1;
    NormalNode.Size = 0;
    NormalNode.View = nullptr;
    OpenMappedFileNode(&NormalNode, "Vertex_normals");
    const unsigned long* NormalView = (const unsigned long*)NormalNode.View;

    const unsigned int VertexCount = (unsigned int)(PointNode.Size / 12);
    Arch->VertexCount = (int)VertexCount;
    unsigned long* Vertices =
        (unsigned long*)new unsigned char[(size_t)VertexCount * 0x18];
    Arch->Vertices = Vertices;
    for (unsigned int Index = 0; Index < (unsigned int)Arch->VertexCount; ++Index)
    {
        unsigned long* Out = Vertices + Index * 6;
        Out[0] = PointView[Index * 3 + 0];
        Out[1] = PointView[Index * 3 + 1];
        Out[2] = PointView[Index * 3 + 2];
        Out[3] = NormalView[Index * 3 + 0];
        Out[4] = NormalView[Index * 3 + 1];
        Out[5] = NormalView[Index * 3 + 2];
    }
    CloseMappedFileNode(&NormalNode);
    DropFileSystem(&NormalNode.Fs);
    CloseMappedFileNode(&PointNode);
    DropFileSystem(&PointNode.Fs);

    FillFileDesc(&Desc, "UV0_indices");
    const int UvIndexHandle = Fs->OpenByDesc(&Desc);
    if (UvIndexHandle != -1)
    {
        const unsigned long UvIndexBytes =
            Fs->GetSize((unsigned long)UvIndexHandle, 0);
        unsigned char* UvIndices = new unsigned char[(size_t)((UvIndexBytes >> 2) << 2)];
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)UvIndexHandle, UvIndices, UvIndexBytes, &Got,
                     0) != 0)
            Arch->UvIndices = (unsigned long*)UvIndices;
        else
            delete[] UvIndices;
        Fs->CloseFile((unsigned long)UvIndexHandle);
    }

    FillFileDesc(&Desc, "UV0");
    const int UvHandle = Fs->OpenByDesc(&Desc);
    if (UvHandle != -1)
    {
        const unsigned long UvBytes = Fs->GetSize((unsigned long)UvHandle, 0);
        const unsigned long UvCount = UvBytes >> 3;
        unsigned char* UvData = new unsigned char[(size_t)UvCount * 8];
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)UvHandle, UvData, UvBytes, &Got, 0) != 0)
        {
            Arch->UvCount = (int)UvCount;
            Arch->UvData = UvData;
        }
        else
        {
            delete[] UvData;
        }
        Fs->CloseFile((unsigned long)UvHandle);
    }

    FMappedFileNode CountNode;
    InitMappedFileNode(&CountNode, Fs);
    MapFileNode(&CountNode, "Point_bone_count", 0xFFFFFFFF);
    const unsigned long* BoneCounts = (const unsigned long*)CountNode.View;

    FMappedFileNode IdNode;
    InitMappedFileNode(&IdNode, Fs);
    MapFileNode(&IdNode, "Bone_id_chain", 0xFFFFFFFF);
    const unsigned long* BoneIds = (const unsigned long*)IdNode.View;

    FMappedFileNode WeightNode;
    InitMappedFileNode(&WeightNode, Fs);
    MapFileNode(&WeightNode, "Bone_weight_chain", 0xFFFFFFFF);
    const unsigned long* Weights = (const unsigned long*)WeightNode.View;

    const unsigned int PairCount = (unsigned int)(IdNode.Size >> 2);
    Arch->SkinPairs = (FSkinPair*)new unsigned char[(size_t)PairCount * 8];
    for (unsigned int Index = 0; Index < PairCount; ++Index)
    {
        unsigned long* Pair = (unsigned long*)(&Arch->SkinPairs[Index]);
        Pair[0] = BoneIds[Index];
        Pair[1] = Weights[Index];
    }

    FSkinPair* Cursor = Arch->SkinPairs;
    Arch->SkinTable = (FSkinVertex*)new unsigned char[(size_t)(unsigned int)Arch->VertexCount * 8];
    for (unsigned int Index = 0; Index < (unsigned int)Arch->VertexCount; ++Index)
    {
        const unsigned long Influence = BoneCounts[Index];
        Arch->SkinTable[Index].BoneCount = (int)Influence;
        Arch->SkinTable[Index].Pairs = Cursor;
        Cursor += Influence;
    }

    CloseMappedFileNode(&WeightNode);
    CloseMappedFileNode(&IdNode);
    CloseMappedFileNode(&CountNode);
    Fs->OpenNode(G_ParentNode); // 065f8349
    return true;
}

// FUN_065f8480 (thiscall on the mesh, RET 4: Fs), 065f8480..89d5, 0x108-byte frame.
static bool LoadUvDeformTables(FMeshArchetype* Arch, IFileSystem* Fs)
{
    if (Fs->OpenNode("Geometry") == 0)
        return false;

    FMappedFileNode BoneIdNode;
    HoldFileSystem(&BoneIdNode.Fs, Fs);
    BoneIdNode.Size = 0;
    BoneIdNode.View = nullptr;
    OpenMappedFileNode(&BoneIdNode, "UV_bone_id");

    FMappedFileNode VertexCountNode;
    HoldFileSystem(&VertexCountNode.Fs, Fs);
    VertexCountNode.Handle = -1;
    VertexCountNode.Size = 0;
    VertexCountNode.View = nullptr;
    OpenMappedFileNode(&VertexCountNode, "UV_vertex_count");

    FMappedFileNode PlaneNode;
    HoldFileSystem(&PlaneNode.Fs, Fs);
    PlaneNode.Handle = -1;
    PlaneNode.Size = 0;
    PlaneNode.View = nullptr;
    OpenMappedFileNode(&PlaneNode, "UV_plane_distance");

    FMappedFileNode XToUNode;
    HoldFileSystem(&XToUNode.Fs, Fs);
    XToUNode.Handle = -1;
    XToUNode.Size = 0;
    XToUNode.View = nullptr;
    OpenMappedFileNode(&XToUNode, "Bone_X_to_U_scale");

    FMappedFileNode YToVNode;
    HoldFileSystem(&YToVNode.Fs, Fs);
    YToVNode.Handle = -1;
    YToVNode.Size = 0;
    YToVNode.View = nullptr;
    MapFileNode(&YToVNode, "Bone_Y_to_V_scale", 0xFFFFFFFF);

    FMappedFileNode MinDuNode;
    HoldFileSystem(&MinDuNode.Fs, Fs);
    MinDuNode.Handle = -1;
    MinDuNode.Size = 0;
    MinDuNode.View = nullptr;
    MapFileNode(&MinDuNode, "Min_du", 0xFFFFFFFF);

    FMappedFileNode MaxDuNode;
    HoldFileSystem(&MaxDuNode.Fs, Fs);
    MaxDuNode.Handle = -1;
    MaxDuNode.Size = 0;
    MaxDuNode.View = nullptr;
    MapFileNode(&MaxDuNode, "Max_du", 0xFFFFFFFF);

    FMappedFileNode MinDvNode;
    HoldFileSystem(&MinDvNode.Fs, Fs);
    MinDvNode.Handle = -1;
    MinDvNode.Size = 0;
    MinDvNode.View = nullptr;
    MapFileNode(&MinDvNode, "Min_dv", 0xFFFFFFFF);

    FMappedFileNode MaxDvNode;
    InitMappedFileNode(&MaxDvNode, Fs);
    MapFileNode(&MaxDvNode, "Max_dv", 0xFFFFFFFF);

    const unsigned long* const Columns[9] = {
        (const unsigned long*)BoneIdNode.View,
        (const unsigned long*)VertexCountNode.View,
        (const unsigned long*)PlaneNode.View,
        (const unsigned long*)XToUNode.View,
        (const unsigned long*)YToVNode.View,
        (const unsigned long*)MinDuNode.View,
        (const unsigned long*)MaxDuNode.View,
        (const unsigned long*)MinDvNode.View,
        (const unsigned long*)MaxDvNode.View,
    };
    Arch->UvDeformCount = (int)(BoneIdNode.Size >> 2);
    if (Arch->UvDeformCount != 0)
    {
        Arch->UvDeform = (FUvDeformRecord*)new unsigned char[(size_t)(unsigned int)Arch->UvDeformCount * 0x24];
        for (unsigned int Index = 0; Index < (unsigned int)Arch->UvDeformCount; ++Index)
        {
            unsigned long* Row = (unsigned long*)(&Arch->UvDeform[Index]);
            for (int Column = 0; Column < 9; ++Column)
                Row[Column] = Columns[Column][Index];
        }
    }

    CloseMappedFileNode(&MaxDvNode);
    CloseMappedFileNode(&MinDvNode);
    CloseMappedFileNode(&MaxDuNode);
    CloseMappedFileNode(&MinDuNode);
    CloseMappedFileNode(&YToVNode);
    CloseMappedFileNode(&XToUNode);
    CloseMappedFileNode(&PlaneNode);
    CloseMappedFileNode(&VertexCountNode);
    CloseMappedFileNode(&BoneIdNode);
    Fs->OpenNode(G_ParentNode); // 065f89c0
    return true;
}

// FUN_065f89e0 (thiscall on the mesh, RET 8: Holder, Fs), 065f89e0..8af9. Each loader in turn,
static bool LoadMeshTables(FMeshArchetype* Mesh, FLodHolder* Holder, IFileSystem* Fs)
{
    if (!LoadFaceGroups(Mesh, Fs))
        return false;
    if (!LoadGeometry(Mesh, Fs))
        return false;
    if (!LoadUvDeformTables(Mesh, Fs))
        return false;
    BindUvBoneIdsToFaceGroups(Mesh);
    BuildVertexRemap(Mesh);
    BuildBoneVertexLists(Mesh, Holder, Fs);

    Mesh->FaceFlagCount = Mesh->VertexCount;
    Mesh->FaceFlags = new unsigned char[(size_t)Mesh->FaceFlagCount];
    std::memset(Mesh->FaceFlags, 0, (size_t)Mesh->FaceFlagCount);
    Mesh->ParseTag = G_ParseTagUtf;
    return true;
}

// FUN_065fdc60 (reached through FUN_065fdca0, vtable slot 6): wrapper vtable, +0xC != -1 ->
// IEngine +0x3C on it; +0x10 non-null -> Release, +0x10 = 0; then the base vtable.
FVmeshWrapper::~FVmeshWrapper()
{
    if (EngineHandle != G_InvalidEngineHandle)
        G_Engine->ReleaseHandle(EngineHandle);
    if (VMeshPart != nullptr)
    {
        VMeshPart->Release();
        VMeshPart = nullptr;
    }
}

// FUN_065fdcc0 (thiscall RET 0xC): +4 = 1, +8 = CRC32(name) (the base ctor), +0xC = handle,
// +0x10 = the VMesh part, AddRef'd when non-null; vtable PTR_LAB_066013dc.
FVmeshWrapper::FVmeshWrapper(const char* Name, void* Handle, IVMeshPart* Part)
    : FDeformMesh(Name)
    , EngineHandle(Handle)
    , VMeshPart(Part)
{
    if (Part != nullptr)
        Part->AddRef();
}

// FUN_065fdf80 (thiscall on Dest, RET 4: Src): MOVSD 0xC dwords (0x30). No tests (until
// 2026-09-30 the port returned on a null pointer).
static void VmeshCopy12(void* Dest, const void* Src)
{
    std::memcpy(Dest, Src, 0x30);
}

// FUN_065f28b0 (cdecl): _vsnprintf(Buffer, 0x2000, Format, ...) and return Buffer. Every
// caller hands it a 0x2000-byte stack buffer.
char* FormatDeformMessage(char* Buffer, const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, 0x2000, Format, Args);
    va_end(Args);
    return Buffer;
}

// FUN_065f26f0 (cdecl: Holder, MeshFs, Name, bFlag70), 065f26f0..28a1.
static FDeformMesh* LoadDeformMesh(FLodHolder* Holder, IFileSystem* MeshFs, const char* Name,
                                   unsigned char bFlag70)
{
    const unsigned long Key = DACOM_CRC::GetCRC32(Name);
    std::map<unsigned long, FDeformMesh*>::iterator Found = G_MeshCache.find(Key);
    if (Found != G_MeshCache.end())
    {
        FDeformMesh* Cached = Found->second;
        AddRefDeformMesh(Cached);
        return Cached;
    }

    FDeformMesh* Result = nullptr;
    FMeshArchetype* Mesh = new FMeshArchetype(Name);
    if (LoadMeshTables(Mesh, Holder, MeshFs))
    {
        Mesh->Flag70 = bFlag70;
        Result = Mesh;
    }
    else
    {
        ReleaseDeformMesh(Mesh);
        FEngineName ArchName;
        ArchName.Text = Name;
        ArchName.Crc = DACOM_CRC::GetCRC32(Name);
        void* Handle = G_Engine->CreateArchetypeHandle(&ArchName, MeshFs);
        IVMeshPart* Library = nullptr;
        // This query ABI accepts the const IID as an opaque mutable token.
        if (G_Engine->QueryEngineComponent((int)((intptr_t)Handle),
                                           const_cast<char*>(Iids::VMeshLibrary),
                                           (void**)(&Library)) < 0)
        {
            G_Engine->ReleaseHandle(Handle);
        }
        else
        {
            Result = new FVmeshWrapper(Name, Handle, Library);
        }
        if (Library != nullptr)
            Library->Release();
    }

    if (Result == nullptr)
    {
        char Message[0x2000];
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableMesh, 0x39,
              FormatDeformMessage(Message, "Deformable: Could not load mesh %s", Name));
        return nullptr;
    }
    G_MeshCache[Key] = Result;
    return Result;
}

// FUN_065f13e0 thiscall: 3x4 identity (diag 1.0f).
static void WriteIdentity3x4(float* Matrix)
{
    std::memset(Matrix, 0, 12 * sizeof(float));
    Matrix[0] = 1.0f;
    Matrix[4] = 1.0f;
    Matrix[8] = 1.0f;
}

// The skeleton ctor FUN_065f36c0 inlines at 065f3879..38c4: operator new(0x1e88), +0 = 1.0f,
// +4 = 0, each of the 150 bones' +0 = -1 (FUN_065f3e70 on +4 is an empty ctor -- the matrices
// stay uninitialised until FUN_065f1100 copies them), +0x1e80 = -1, +0x1e84 = 1.
static FSkeleton* ConstructSkeleton()
{
    FSkeleton* Skel = new FSkeleton;
    Skel->Scale = 1.0f;
    Skel->BoneCount = 0;
    int Index = 0;
    while (Index < G_MaxBoneCount)
    {
        Skel->Bones[Index].Parent = -1;
        Index += 1;
    }
    Skel->EngineHandle = G_InvalidEngineHandle;
    Skel->RefCount = 1;
    return Skel;
}

// FUN_065f10c0 (thiscall), 065f10c0..10fd: IEngine +0x3C on each loaded bone's +0 handle,
// then on +0x1e80 -- unconditionally, -1 included (a skeleton whose FUN_065f1100 failed still
// passes its -1 here). Until 2026-09-30 the port skipped null/-1 handles and a null engine.
static void DestroySkeletonHandles(FSkeleton* Skel)
{
    int Index = 0;
    while (Index < Skel->BoneCount)
    {
        G_Engine->ReleaseHandle(
            (void*)((ptrdiff_t)(Skel->Bones[Index].Parent)));
        Index += 1;
    }
    G_Engine->ReleaseHandle(Skel->EngineHandle);
}

// FUN_065f1410 thiscall RET 0.
static void AddRefSkeleton(FSkeleton* Skel)
{
    if (Skel == nullptr)
        return;
    Skel->RefCount += 1;
}

// FUN_065f1420 thiscall: DEC ref, if <=0 then 10c0 + operator_delete.
static int ReleaseSkeleton(FSkeleton* Skel)
{
    if (Skel == nullptr)
        return 0;
    Skel->RefCount -= 1;
    const int Remaining = Skel->RefCount;
    if (Remaining <= 0)
    {
        DestroySkeletonHandles(Skel);
        delete Skel;
    }
    return Remaining;
}

// FUN_065f1470 thiscall RET 0: delete BoneSlots, ReleaseSkeleton, null +0.
static void DestroyLodHolderBody(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return;
    if (Holder->BoneSlots != nullptr)
    {
        delete[] (unsigned char*)Holder->BoneSlots;
        Holder->BoneSlots = nullptr;
    }
    if (Holder->Skeleton != nullptr)
    {
        ReleaseSkeleton(Holder->Skeleton);
        Holder->Skeleton = nullptr;
    }
}

// FUN_065f17d0 thiscall RET 0: INC holder+0xC.
static void AddRefLodHolder(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return;
    Holder->RefCount += 1;
}

// FUN_065f17e0 thiscall: DEC +0xC, if <=0 then 1470 + operator_delete.
static int ReleaseLodHolder(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return 0;
    Holder->RefCount -= 1;
    const int Remaining = Holder->RefCount;
    if (Remaining <= 0)
    {
        DestroyLodHolderBody(Holder);
        delete Holder;
    }
    return Remaining;
}

// FUN_065f1100 (thiscall on the skeleton, RET 8: Name, Fs), 065f1100..13d3.
static bool LoadSkeleton(FSkeleton* Skel, const char* Name, IFileSystem* Fs)
{
    WIN32_FIND_DATAA FindData;
    const int Finder = FsFindFirst(Fs, "*.3db", &FindData);
    if (Finder != -1)
    {
        do
        {
            FFileSystemDesc ChildDesc;
            FillFileDesc(&ChildDesc, FindData.cFileName);
            ChildDesc.Field30 = (unsigned long)Finder;
            IFileSystem* Child = nullptr;
            if (Fs->CreateInstance((AGGDESC*)(&ChildDesc),
                                   (void**)(&Child)) >= 0)
            {
                const int bSkeletonNode = Child->OpenNode("DFMSkeleton");
                FBoneSlot* Bone = &Skel->Bones[Skel->BoneCount];
                float Buffer[12];
                WriteIdentity3x4(Buffer);
                FFileSystemDesc BoneDesc;
                FillFileDesc(&BoneDesc, "Bone to root");
                bool bRead = false;
                const int Handle = Child->OpenByDesc(&BoneDesc);
                if (Handle != -1)
                {
                    unsigned long Got = 0;
                    if (Child->Read((unsigned long)Handle, Buffer, 0x30, &Got, 0) != 0 &&
                        Got == 0x30)
                    {
                        std::memcpy(Bone->BoneToRoot, Buffer, 0x30);
                        bRead = true;
                    }
                    Child->CloseFile((unsigned long)Handle);
                }
                const GENRESULT Result = bRead ? 0 : (GENRESULT)0x80004005;
                if (bSkeletonNode != 0)
                    Fs->OpenNode(G_ParentNode); // 065f12c4
                if (Result >= 0)
                {
                    FEngineName BoneName;
                    BoneName.Text = FindData.cFileName;
                    BoneName.Crc = DACOM_CRC::GetCRC32(FindData.cFileName);
                    Bone->Parent = (int)((ptrdiff_t)(
                        G_Engine->CreateArchetypeHandle(&BoneName, Child)));
                    Skel->BoneCount += 1;
                }
            }
            if (Child != nullptr)
                Child->Release();
        } while (FsFindNext(Fs, Finder, &FindData) != 0);
        FsFindClose(Fs, Finder);
    }

    IDAComponent* Saved = nullptr;
    G_Engine->GetSearchPath((void**)(&Saved));
    G_Engine->SetFilesystem(Fs);
    FEngineName SkelName;
    SkelName.Text = Name;
    SkelName.Crc = DACOM_CRC::GetCRC32(Name);
    Skel->EngineHandle = G_Engine->CreateArchetypeHandle(&SkelName, Fs);
    G_Engine->SetFilesystem(Saved);
    Saved->Release();
    return Skel->EngineHandle != G_InvalidEngineHandle;
}

// --- FBoneRuntime : IInstanceTransform, vtable PTR_LAB_066010a0 (14 slots) ---
// Every slot is stdcall with the engine instance as its first argument (EngBase pushes it);
// only slot 0 uses it. None of them tests `this` or a pointer argument.

// FUN_065f16a0 (1 instruction, RET 8): slots 1 and 2, and the ICF-shared empty body of
// several IEngineComponent slots. EAX is not set; the port answers 0.
static unsigned long DACOM_API BoneRuntimeRet8(void* /*This*/, void* /*A*/)
{
    return 0;
}

// LAB_065f1790 (1 instruction, RET 0xC): slot 11 SetUser, shared with IEngineComponent
// slots 5/10. EAX is not set.
static void DACOM_API BoneRuntimeRetC(void* /*This*/, void* /*A*/, void* /*B*/)
{
}

static void WriteIdentity3x3(float* Matrix);

// The per-slot body of the FUN_065f1550 construct loop (065f15a0..15c8): +4 = -1 (065f15a3),
FBoneRuntime::FBoneRuntime()
{
    Instance = G_InvalidEngineHandle;
    Source = nullptr;
    Flags = 0;
    std::memset(FlagPadding, 0, sizeof(FlagPadding));
    WriteIdentity3x3(Matrix);
    Matrix[9] = 0.0f;
    Matrix[10] = 0.0f;
    Matrix[11] = 0.0f;
}

// LAB_065f1690 (RET 8), slot 0: [this+4] = the instance.
GENRESULT DACOM_API FBoneRuntime::Init(void* InInstance)
{
    Instance = InInstance;
    return GR_OK; // EAX = this (065f1690 loads it); nobody reads it
}

// FUN_065f16a0, slot 1.
GENRESULT DACOM_API FBoneRuntime::AddRef(void* InInstance)
{
    return (GENRESULT)(BoneRuntimeRet8(this, InInstance));
}

// FUN_065f16a0, slot 2.
GENRESULT DACOM_API FBoneRuntime::Release(void* InInstance)
{
    return (GENRESULT)(BoneRuntimeRet8(this, InInstance));
}

// LAB_065f16b0 (RET 0xC), slot 3: three dwords of Position to +0x30; bit 0 of +0x3c set ->
// |= 2.
GENRESULT DACOM_API FBoneRuntime::SetPosition(void* /*InInstance*/, const float* Position)
{
    float* const Dest = LocalTransform + 9;
    Dest[0] = Position[0];
    Dest[1] = Position[1];
    Dest[2] = Position[2];
    if ((this->Flags & 1u) != 0)
        this->Flags = (unsigned char)(this->Flags | 2u);
    return GR_OK; // EAX = the byte read (065f16cc); nobody reads it
}

// LAB_065f16e0 (RET 8), slot 4: this + 0x30.
float* DACOM_API FBoneRuntime::GetPosition(void* /*InInstance*/)
{
    return LocalTransform + 9;
}

// LAB_065f16f0 (RET 0xC), slot 5: REP MOVSD of 9 dwords Rows -> +0x0C, always (065f1707);
// the bit 0 test made before it (065f16f7) only decides the |= 2 after it (065f170b).
GENRESULT DACOM_API FBoneRuntime::SetOrientation(void* /*InInstance*/, const float* Rows)
{
    const unsigned char Flags = this->Flags;
    std::memcpy(LocalTransform, Rows, 9 * sizeof(float));
    if ((Flags & 1u) != 0)
        this->Flags = (unsigned char)(Flags | 2u);
    return GR_OK;
}

// LAB_065f1720 (RET 8), slots 6 and 8: this + 0x0C.
float* DACOM_API FBoneRuntime::GetOrientation(void* /*InInstance*/)
{
    return LocalTransform;
}

// LAB_065f1730 (RET 0xC), slot 7: REP MOVSD of 12 dwords (rotation + position) -> +0x0C,
// always (065f1747); bit 0 tested before (065f1737) decides only the |= 2 (065f174b).
GENRESULT DACOM_API FBoneRuntime::SetMatrix(void* /*InInstance*/, const float* InMatrix)
{
    const unsigned char Flags = this->Flags;
    std::memcpy(LocalTransform, InMatrix, 12 * sizeof(float));
    if ((Flags & 1u) != 0)
        this->Flags = (unsigned char)(Flags | 2u);
    return GR_OK;
}

// LAB_065f1720, slot 8 (the same body as slot 6).
float* DACOM_API FBoneRuntime::GetMatrix(void* /*InInstance*/)
{
    return LocalTransform;
}

// LAB_065f1760 (RET 0x10), slot 9: *[ESP+0xC] = 0 and the three dwords at [ESP+0x10] = 0 --
// the second and third arguments; the instance is not read.
GENRESULT DACOM_API FBoneRuntime::GetExtra(void* /*InInstance*/, float* OutExtra, float* OutVec)
{
    *(unsigned long*)OutExtra = 0;
    ((unsigned long*)OutVec)[0] = 0;
    ((unsigned long*)OutVec)[1] = 0;
    ((unsigned long*)OutVec)[2] = 0;
    return GR_OK; // EAX = OutVec (065f1768); nobody reads it
}

// LAB_065f1780 (1 instruction, RET 0x10), slot 10.
GENRESULT DACOM_API FBoneRuntime::SetExtra(void* /*InInstance*/, float /*Extra*/,
                                           const float* /*Vec*/)
{
    return GR_OK;
}

// LAB_065f1790 (RET 0xC), slot 11.
GENRESULT DACOM_API FBoneRuntime::SetUser(void* InInstance, unsigned long User)
{
    BoneRuntimeRetC(this, InInstance, (void*)((ptrdiff_t)User));
    return GR_OK;
}

// LAB_065f17a0 (RET 8), slot 12: XOR EAX,EAX.
unsigned long DACOM_API FBoneRuntime::GetUser(void* /*InInstance*/)
{
    return 0;
}

// LAB_065f17b0 (RET 8), slot 13: bit 1 of +0x3c set -> AL = 0; else |= 2 and EAX =
// (~byte) & 1 (NOT AL; AND EAX,1 -- the AND clears EAX's upper bytes).
unsigned long DACOM_API FBoneRuntime::MarkWorldDirty(void* /*InInstance*/) // formerly Slot13
{
    unsigned char Flags = this->Flags;
    if ((Flags & 2u) != 0)
        return 0;
    Flags = (unsigned char)(Flags | 2u);
    this->Flags = Flags;
    return (unsigned long)((~Flags) & 1u);
}

static I3DMathEngine* AcquireMathEngine();

// The FUN_065f1550 construct loop body for one slot. The memset stands in for the operator
// new garbage in the fields retail leaves unwritten (+0x0C..+0x3B). No test of the slot.
static void InitBoneRuntimeSlot(FBoneRuntime* Slot)
{
    std::memset((void*)Slot, 0, sizeof(FBoneRuntime));
    new (Slot) FBoneRuntime();
}

// FUN_065f1660 thiscall RET 0, 065f1660..1683: +0x00 / +0x10 / +0x20 = 1.0f, the other six
// dwords of the 3x3 = 0. No test (until 2026-09-30 the port returned on a null matrix).
static void WriteIdentity3x3(float* Matrix)
{
    std::memset(Matrix, 0, 9 * sizeof(float));
    Matrix[0] = 1.0f;
    Matrix[4] = 1.0f;
    Matrix[8] = 1.0f;
}

// FUN_065f1030 thiscall RET 0x4 (InverseRoot), 065f1030..10b2: math (DAT_06603120,
static void ComposeBoneWorldMatrix(FBoneRuntime* Slot, const float* InverseRoot)
{
    float Local[12];
    AcquireMathEngine()->MulTransform(
        *(FTransform*)Local,
        *(const FTransform*)Slot->LocalTransform,
        *(const FTransform*)Slot->Source->BoneToRoot); // +0x1C
    float LocalCopy[12];
    std::memcpy(LocalCopy, Local, sizeof(LocalCopy));
    float World[12];
    AcquireMathEngine()->MulTransform(*(FTransform*)World,
                                      *(const FTransform*)InverseRoot,
                                      *(const FTransform*)LocalCopy); // +0x1C
    std::memcpy(Slot->Matrix, World, sizeof(World));
}

// FUN_065f14a0 (thiscall on the holder, RET 8: Index, BoneInst), 065f14a0..14e3: slot =
static FBoneRuntime* BindOneBoneRuntime(FLodHolder* Holder, int Index, void* BoneInst)
{
    FBoneRuntime* Slot = &Holder->BoneSlots[Index];
    unsigned char Flags = Slot->Flags;
    Flags = (unsigned char)(((Index == 0) ^ Flags) & 1 ^ Flags);
    Slot->Flags = Flags;
    Slot->Source = &Holder->Skeleton->Bones[Index];
    G_Engine->SetInstanceTransform(BoneInst, Slot);
    return Slot;
}

static float SvdAbs(float Value)
{
    return (Value < 0.0f) ? -Value : Value;
}

// Shared scratch used by the SVD scalar operations.
static float G_SvdSqrArg = 0.0f;
static float G_SvdMaxArg1 = 0.0f;
static float G_SvdMaxArg2 = 0.0f;
static int G_SvdMinArg1 = 0;
static int G_SvdMinArg2 = 0;

// FUN_065f9e00 (cdecl-shaped, ECX unused, RET 8: two floats), 065f9e00..9ec0. The answer is
static double SvdPythag(float Left, float Right)
{
    const double AbsLeft = std::fabs((double)Left);
    const float AbsRight = std::fabs(Right);
    if (AbsLeft > (double)AbsRight)
    {
        const double Ratio = (double)AbsRight / AbsLeft;
        G_SvdSqrArg = (float)Ratio;
        if (Ratio == 0.0)
            return AbsLeft * std::sqrt(0.0 + 1.0);
        const double RatioF = (double)G_SvdSqrArg;
        return AbsLeft * std::sqrt(RatioF * RatioF + 1.0);
    }
    if ((double)AbsRight == 0.0)
        return 0.0;
    const double Ratio = AbsLeft / (double)AbsRight;
    G_SvdSqrArg = (float)Ratio;
    if (Ratio == 0.0)
        return std::sqrt(0.0 + 1.0) * (double)AbsRight;
    const double RatioF = (double)G_SvdSqrArg;
    return std::sqrt(RatioF * RatioF + 1.0) * (double)AbsRight;
}

// FUN_065f9ed0 (cdecl: Value, Anorm): FLD Value, FADD Anorm, FSTP float, FCOMP Anorm; equal ->
// AL 1. A float + float rounded once to float is what SSE float addition gives.
static bool SvdIsNegligible(float Value, float Anorm)
{
    return (Value + Anorm) == Anorm;
}

static float& SvdCell(float* Base, int Row, int Col)
{
    return Base[Row * G_SvdMaxDim + Col];
}

// The sign test before each FSQRT / FABS: FCOM(P) against 0.0 then TEST AH,1 + JZ -- the FCHS
// runs when C0 is set, i.e. below zero OR unordered (a NaN counts as negative).
static bool SvdSignC0(float Value)
{
    return !((double)Value >= 0.0);
}

// |Value| negated unless the sign test set C0: the FABS / (C0 ? FCHS) / FCHS tail both
// Householder halves use.
static double SvdNegSignOf(double Value, bool bSignNegative)
{
    double Magnitude = std::fabs(Value);
    if (bSignNegative)
        Magnitude = -Magnitude;
    return -Magnitude;
}

// FUN_065f9f00 (thiscall on the source matrix, RET 0xC: A, W, V), 065f9f00..ac45: Golub-Reinsch
static unsigned char SvdCmp(FSvdMatrix* Source, float* A, float* W, float* V)
{
    float Rv1[12];
    float G = 0.0f;
    float Anorm = 0.0f;
    double Scale = 0.0;
    int L = 0;
    int I = 0;
    while (I < Source->ColCount)
    {
        L = I + 1;
        Rv1[I] = (float)(Scale * (double)G);
        G = 0.0f;
        Scale = 0.0;
        double Sum = 0.0;
        if (I < Source->RowCount)
        {
            for (int K = I; K < Source->RowCount; ++K)
                Scale += std::fabs((double)(SvdCell(A, K, I)));
            if (Scale != 0.0)
            {
                const double Inv = 1.0 / Scale;
                for (int K = I; K < Source->RowCount; ++K)
                {
                    const double Scaled = Inv * (double)(SvdCell(A, K, I));
                    SvdCell(A, K, I) = (float)Scaled;
                    Sum += Scaled * Scaled;
                }
                const float F = SvdCell(A, I, I);
                G = (float)(SvdNegSignOf(std::sqrt(Sum), SvdSignC0(F)));
                const double H = (double)F * (double)G - Sum;
                SvdCell(A, I, I) = (float)((double)F - (double)G);
                if (L < Source->ColCount)
                {
                    const double HInv = 1.0 / H;
                    for (int J = L; J < Source->ColCount; ++J)
                    {
                        double Dot = 0.0;
                        if (I < Source->RowCount)
                        {
                            for (int K = I; K < Source->RowCount; ++K)
                                Dot += (double)(SvdCell(A, K, J)) *
                                       (double)(SvdCell(A, K, I));
                        }
                        const float Factor = (float)(HInv * Dot);
                        if (I < Source->RowCount)
                        {
                            for (int K = I; K < Source->RowCount; ++K)
                                SvdCell(A, K, J) = (float)(
                                    (double)Factor * SvdCell(A, K, I) +
                                    (double)(SvdCell(A, K, J)));
                        }
                    }
                }
                for (int K = I; K < Source->RowCount; ++K)
                    SvdCell(A, K, I) = (float)(Scale * (double)(SvdCell(A, K, I)));
            }
        }
        W[I] = (float)(Scale * (double)G);

        G = 0.0f;
        Scale = 0.0;
        Sum = 0.0;
        if (I < Source->RowCount && I != Source->ColCount && L < Source->ColCount)
        {
            for (int K = L; K < Source->ColCount; ++K)
                Scale += std::fabs((double)(SvdCell(A, I, K)));
            if (Scale != 0.0)
            {
                const double Inv = 1.0 / Scale;
                for (int K = L; K < Source->ColCount; ++K)
                {
                    const double Scaled = Inv * (double)(SvdCell(A, I, K));
                    SvdCell(A, I, K) = (float)Scaled;
                    Sum += Scaled * Scaled;
                }
                const float SumF = (float)Sum;
                const float F = SvdCell(A, I, L);
                const double Gd = SvdNegSignOf(std::sqrt((double)SumF), SvdSignC0(F));
                G = (float)Gd;
                const float H = (float)(Gd * (double)F -
                                                   (double)SumF);
                SvdCell(A, I, L) = (float)((double)F - (double)G);
                if (L < Source->ColCount)
                {
                    const double HInv = 1.0 / (double)H;
                    for (int K = L; K < Source->ColCount; ++K)
                        Rv1[K] = (float)(HInv * (double)(SvdCell(A, I, K)));
                }
                if (L < Source->RowCount)
                {
                    for (int J = L; J < Source->RowCount; ++J)
                    {
                        double Dot = 0.0;
                        if (L < Source->ColCount)
                        {
                            for (int K = L; K < Source->ColCount; ++K)
                                Dot += (double)(SvdCell(A, I, K)) *
                                       (double)(SvdCell(A, J, K));
                        }
                        if (L < Source->ColCount)
                        {
                            for (int K = L; K < Source->ColCount; ++K)
                                SvdCell(A, J, K) = (float)(
                                    Dot * (double)Rv1[K] +
                                    (double)(SvdCell(A, J, K)));
                        }
                    }
                }
                if (L < Source->ColCount)
                {
                    for (int K = L; K < Source->ColCount; ++K)
                        SvdCell(A, I, K) = (float)(Scale * (double)(SvdCell(A, I, K)));
                }
            }
        }
        G_SvdMaxArg1 = Anorm;
        G_SvdMaxArg2 = (float)(std::fabs((double)W[I]) +
                                          std::fabs((double)Rv1[I]));
        if (!(Anorm > G_SvdMaxArg2))
            Anorm = G_SvdMaxArg2;
        I = L;
    }

    // 065fa340..a506: right-hand transforms into V, i from n-1 down; g (float) and l carry.
    I = Source->ColCount - 1;
    while (I >= 0)
    {
        if (I < Source->ColCount)
        {
            if (G != 0.0f)
            {
                if (L < Source->ColCount)
                {
                    const double GInv = 1.0 / (double)G;
                    for (int J = L; J < Source->ColCount; ++J)
                        SvdCell(V, J, I) = (float)(
                            (double)(SvdCell(A, I, J)) /
                            (double)(SvdCell(A, I, L)) * GInv);
                }
                if (L < Source->ColCount)
                {
                    for (int J = L; J < Source->ColCount; ++J)
                    {
                        double Dot = 0.0;
                        if (L < Source->ColCount)
                        {
                            for (int K = L; K < Source->ColCount; ++K)
                                Dot += (double)(SvdCell(V, K, J)) *
                                       (double)(SvdCell(A, I, K));
                        }
                        if (L < Source->ColCount)
                        {
                            for (int K = L; K < Source->ColCount; ++K)
                                SvdCell(V, K, J) = (float)(
                                    Dot * (double)(SvdCell(V, K, I)) +
                                    (double)(SvdCell(V, K, J)));
                        }
                    }
                }
            }
            if (L < Source->ColCount)
            {
                for (int J = L; J < Source->ColCount; ++J)
                {
                    SvdCell(V, J, I) = 0.0f;
                    SvdCell(V, I, J) = 0.0f;
                }
            }
        }
        SvdCell(V, I, I) = 1.0f;
        G = Rv1[I];
        L = I;
        I -= 1;
    }

    // 065fa50c..a6f0: left-hand transforms into A, i from min(m, n)-1 down (IMIN through
    // DAT_066032a4 / DAT_066032a0).
    G_SvdMinArg1 = Source->RowCount;
    G_SvdMinArg2 = Source->ColCount;
    const int MinDim = (G_SvdMinArg1 < G_SvdMinArg2) ? G_SvdMinArg1 : G_SvdMinArg2;
    I = MinDim - 1;
    while (I >= 0)
    {
        L = I + 1;
        const float WVal = W[I];
        if (L < Source->ColCount)
        {
            for (int J = L; J < Source->ColCount; ++J)
                SvdCell(A, I, J) = 0.0f;
        }
        if (WVal != 0.0f)
        {
            const float GInv = (float)(1.0 / (double)WVal);
            if (L < Source->ColCount)
            {
                for (int J = L; J < Source->ColCount; ++J)
                {
                    double Dot = 0.0;
                    if (L < Source->RowCount)
                    {
                        for (int K = L; K < Source->RowCount; ++K)
                            Dot += (double)(SvdCell(A, K, I)) *
                                   (double)(SvdCell(A, K, J));
                    }
                    const float Factor = (float)(
                        Dot / (double)(SvdCell(A, I, I)) * (double)GInv);
                    if (I < Source->RowCount)
                    {
                        for (int K = I; K < Source->RowCount; ++K)
                            SvdCell(A, K, J) = (float)(
                                (double)Factor * SvdCell(A, K, I) +
                                (double)(SvdCell(A, K, J)));
                    }
                }
            }
            if (I < Source->RowCount)
            {
                for (int K = I; K < Source->RowCount; ++K)
                    SvdCell(A, K, I) = (float)((double)GInv *
                                                          (double)(SvdCell(A, K, I)));
            }
        }
        else
        {
            if (I < Source->RowCount)
            {
                for (int K = I; K < Source->RowCount; ++K)
                    SvdCell(A, K, I) = 0.0f;
            }
        }
        SvdCell(A, I, I) = (float)((double)(SvdCell(A, I, I)) + 1.0);
        I -= 1;
    }

    // 065fa6f6..ac39: diagonalisation, k from n-1 down, its from 1.
    int K = Source->ColCount - 1;
    while (K >= 0)
    {
        int Its = 1;
        for (;;)
        {
            // 065fa720..a789: l from k down. |rv1[l]| negligible -> no cancellation;
            // |W[l-1]| negligible (or l ran below 0) -> cancellation with this l.
            bool bCancel = true;
            int Nm = K - 1;
            L = K;
            while (L >= 0)
            {
                Nm = L - 1;
                if (SvdIsNegligible(SvdAbs(Rv1[L]), Anorm))
                {
                    bCancel = false;
                    break;
                }
                if (SvdIsNegligible(SvdAbs(W[Nm]), Anorm))
                    break;
                L = Nm;
            }
            if (bCancel && L <= K)
            {
                // 065fa789..a899: c (float) = 0, s (on the FPU, double) = 1.
                float Cos = 0.0f;
                double Sin = 1.0;
                for (int Row = L; Row <= K; ++Row)
                {
                    const float F = (float)(Sin * (double)Rv1[Row]);
                    Rv1[Row] = (float)((double)Cos * Rv1[Row]);
                    if (SvdIsNegligible(SvdAbs(F), Anorm))
                        break;
                    const float Gw = W[Row];
                    const double Hyp = SvdPythag(F, Gw);
                    W[Row] = (float)Hyp;
                    const double HInv = 1.0 / Hyp;
                    Cos = (float)((double)Gw * HInv);
                    Sin = -(HInv * (double)F);
                    for (int J = 0; J < Source->RowCount; ++J)
                    {
                        const double Y = (double)(SvdCell(A, J, Nm));
                        const double Z = (double)(SvdCell(A, J, Row));
                        SvdCell(A, J, Nm) = (float)(Y * Cos + Z * Sin);
                        SvdCell(A, J, Row) = (float)(Z * Cos - Y * Sin);
                    }
                }
            }

            // 065fa8ae..a8c2
            const float Z = W[K];
            if (L == K)
            {
                // 065fabdf..ac23: TEST AH,5 + JP -- only "below zero" falls through.
                if ((double)Z < 0.0)
                {
                    W[K] = -Z;
                    for (int J = 0; J < Source->ColCount; ++J)
                        SvdCell(V, J, K) = -SvdCell(V, J, K);
                }
                break;
            }
            if (Its == G_SvdMaxIter)
                return 0;

            // 065fa8c8..a98b: shift from the bottom 2x2 minor.
            float X = W[L];
            Nm = K - 1;
            float Y = W[Nm];
            float Gv = Rv1[Nm];
            float H = Rv1[K];
            float F = (float)(
                (((double)Y - Z) * ((double)Z + Y) +
                 ((double)Gv - H) * ((double)H + Gv)) /
                ((double)Y * H + (double)Y * H));
            const double GSigned = SvdSignC0(F) ? -std::fabs(SvdPythag(F, 1.0f))
                                                : std::fabs(SvdPythag(F, 1.0f));
            F = (float)(
                (((double)Y / ((double)F + GSigned) - H) * H +
                 ((double)X - Z) * ((double)X + Z)) /
                (double)X);
            float Cos = 1.0f;
            float Sin = 1.0f;

            // 065fa9e0..ab9f: the QR sweep; every quantity is stored as float between steps.
            for (int J = L; J <= Nm; ++J)
            {
                const int Next = J + 1;
                const float GNext = Rv1[Next];
                Y = W[Next];
                H = (float)((double)Sin * GNext);
                Gv = (float)((double)Cos * GNext);
                double Zd = SvdPythag(F, H);
                Rv1[J] = (float)Zd;
                double ZInv = 1.0 / Zd;
                Cos = (float)((double)F * ZInv);
                Sin = (float)(ZInv * (double)H);
                F = (float)((double)X * Cos + (double)Gv * Sin);
                Gv = (float)((double)Cos * Gv - (double)X * Sin);
                H = (float)((double)Y * Sin);
                Y = (float)((double)Y * Cos);
                for (int Jj = 0; Jj < Source->ColCount; ++Jj)
                {
                    const double Vx = (double)(SvdCell(V, Jj, J));
                    const double Vz = (double)(SvdCell(V, Jj, Next));
                    SvdCell(V, Jj, J) = (float)(Vx * Cos + Vz * Sin);
                    SvdCell(V, Jj, Next) = (float)(Vz * Cos - Vx * Sin);
                }
                Zd = SvdPythag(F, H);
                W[J] = (float)Zd;
                if (Zd != 0.0)
                {
                    ZInv = 1.0 / Zd;
                    Cos = (float)((double)F * ZInv);
                    Sin = (float)(ZInv * (double)H);
                }
                F = (float)((double)Y * Sin + (double)Cos * Gv);
                X = (float)((double)Y * Cos - (double)Sin * Gv);
                for (int Jj = 0; Jj < Source->RowCount; ++Jj)
                {
                    const double Ay = (double)(SvdCell(A, Jj, J));
                    const double Az = (double)(SvdCell(A, Jj, Next));
                    SvdCell(A, Jj, J) = (float)(Ay * Cos + Az * Sin);
                    SvdCell(A, Jj, Next) = (float)(Az * Cos - Ay * Sin);
                }
            }
            // 065fabb4..abd7
            W[K] = X;
            Rv1[L] = 0.0f;
            Rv1[K] = F;
            Its += 1;
            if (Its > G_SvdMaxIter)
                break;
        }
        K -= 1;
    }
    return 1;
}

static void SvdScaleColumns(const FSvdMatrix* V, const float* W, FSvdMatrix* Out)
{
    Out->RowCount = V->RowCount;
    Out->ColCount = V->ColCount;
    const int Rows = V->RowCount;
    const int Cols = V->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
            Out->Cell[Row][Col] = W[Col] * V->Cell[Row][Col];
    }
}

static void SvdMulABt(const FSvdMatrix* Left, const FSvdMatrix* Right, FSvdMatrix* Out)
{
    Out->RowCount = Left->RowCount;
    Out->ColCount = Right->RowCount;
    const int Rows = Left->RowCount;
    // Listing 065fad30: Out.n = Right.m, but the EBP loop bound is Right.n (+0x244).
    const int OutCols = Right->ColCount;
    const int Inner = Left->ColCount;
    // 065fad40..ad5f: the cell is zeroed and then accumulated in place -- FLD Right, FMUL Left,
    // FADD cell, FSTP float -- so each step rounds once (the product never does on its own).
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < OutCols; ++Col)
        {
            float Sum = 0.0f;
            for (int K = 0; K < Inner; ++K)
                Sum = (float)((double)Right->Cell[Col][K] *
                                             (double)Left->Cell[Row][K] +
                                         (double)Sum);
            Out->Cell[Row][Col] = Sum;
        }
    }
}

// FUN_065fadb0 (thiscall on the source matrix, RET 4: Out), 065fadb0..aedf: Out.m = Source.n,
static void SvdPseudoInverse(FSvdMatrix* Source, FSvdMatrix* Out)
{
    Out->RowCount = Source->ColCount;
    Out->ColCount = Source->RowCount;
    const int N = Source->ColCount;
    float W[12];
    int Index = 0;
    while (Index < N)
    {
        W[Index] = 0.0f;
        Index += 1;
    }
    FSvdMatrix V;
    V.RowCount = Source->ColCount;
    V.ColCount = Source->ColCount;
    FSvdMatrix Work;
    std::memcpy(&Work, Source, sizeof(FSvdMatrix));

    if (SvdCmp(Source, &Work.Cell[0][0], W, &V.Cell[0][0]) == 0)
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileMat, 0x2E8,
              "svdcmp: didnt approach a finite soln.");

    Index = 0;
    while (Index < N)
    {
        if ((double)W[Index] < 1.0e-6)
            W[Index] = 0.0f;
        else
            W[Index] = (float)(1.0 / (double)W[Index]);
        Index += 1;
    }

    FSvdMatrix Scaled;
    Scaled.RowCount = V.RowCount;
    Scaled.ColCount = V.ColCount;
    SvdScaleColumns(&V, W, &Scaled);
    SvdMulABt(&Scaled, &Work, Out);
}

// FUN_065f9d70 transpose; FUN_065faef0 A*B; FUN_065fafc0 A*x.
static void SvdTranspose(const FSvdMatrix* Source, FSvdMatrix* Out)
{
    Out->RowCount = Source->ColCount;
    Out->ColCount = Source->RowCount;
    const int Rows = Source->RowCount;
    const int Cols = Source->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
            Out->Cell[Col][Row] = Source->Cell[Row][Col];
    }
}

static void SvdMulAB(const FSvdMatrix* Left, const FSvdMatrix* Right, FSvdMatrix* Out)
{
    Out->RowCount = Left->RowCount;
    Out->ColCount = Right->ColCount;
    const int Rows = Left->RowCount;
    const int Cols = Right->ColCount;
    const int Inner = Left->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
        {
            // 065faf52..af6f: cell = 0, then FLD / FMUL / FADD cell / FSTP float per step.
            float Sum = 0.0f;
            for (int K = 0; K < Inner; ++K)
                Sum = (float)((double)Left->Cell[Row][K] *
                                             (double)Right->Cell[K][Col] +
                                         (double)Sum);
            Out->Cell[Row][Col] = Sum;
        }
    }
}

static void SvdMulVec(const FSvdMatrix* Left, const float* Vec, float* Out)
{
    const int Rows = Left->RowCount;
    const int Cols = Left->ColCount;
    // Listing 065fafca: MOV [EDX+0x30], EAX stores integer m bits, not float(m).
    *(int*)(Out + 12) = Rows;
    for (int Row = 0; Row < Rows; ++Row)
    {
        // 065fafe3..b002: out = 0, then FLD / FMUL / FADD out / FSTP float per step.
        float Sum = 0.0f;
        for (int Col = 0; Col < Cols; ++Col)
            Sum = (float)((double)Vec[Col] *
                                         (double)Left->Cell[Row][Col] +
                                     (double)Sum);
        Out[Row] = Sum;
    }
}

// FUN_06600de0: CreateInstance AGGDESC 0xC "3DMathEngine" (Ghidra returns ICOManager — wrong).
static I3DMathEngine* AcquireMathEngine()
{
    // Retail keeps one lazily-filled cache per call site (DAT_06603170, DAT_066031a4,
    if (G_MathEngine != nullptr)
        return G_MathEngine;
    ICOManager* Manager = DACOM_Acquire();
    FIkMathDesc Desc;
    Desc.Size = 0xC;
    Desc.Name = G_Name3DMathEngine;
    Desc.Extra = nullptr;
    void* Instance = nullptr;
    Manager->CreateInstance((AGGDESC*)(&Desc), &Instance);
    G_MathEngine = (I3DMathEngine*)Instance;
    return G_MathEngine;
}

// The math engine calls the IK solvers make, each through DAT_066033a8 (FUN_06600de0 on first
// use, no other test) at every call site. Float-array views of the engine's typed slots; they
// add no checks.
static void IkMathMulMatrix(float* Out9, const float* Left9, const float* Right9)
{
    AcquireMathEngine()->MulMatrix(*(FMatrix*)Out9,
                                   *(const FMatrix*)Left9,
                                   *(const FMatrix*)Right9); // +0x20
}

static void IkMathTransform(float* Out3, const float* Mat9, const float* Vec3)
{
    AcquireMathEngine()->TransformMatrix(*(FVector*)Out3,
                                         *(const FMatrix*)Mat9,
                                         *(const FVector*)Vec3); // +0x2C
}

static void IkMathMatrixToQuat(float* Out4, const float* Mat9)
{
    AcquireMathEngine()->MatrixToQuaternion(*(FQuaternion*)Out4,
                                            *(const FMatrix*)Mat9); // +0x50
}

static void IkMathQuatToMatrix(float* Out9, const float* Quat4)
{
    AcquireMathEngine()->QuaternionToMatrix(*(FMatrix*)Out9,
                                            *(const FQuaternion*)Quat4); // +0x54
}

// FUN_065f7120: DAT_066031a4 / FUN_06600de0; math+0x38(tmp,a,b); copy vec3 to dest.
void MathTransformVec3(float* Dest, const float* A, const float* B)
{
    // FUN_065f7120 (cdecl: Dest, A, B), 065f7120..7168: math (DAT_066031a4, FUN_06600de0 on
    // first use) +0x38 (&Tmp, A, B), Tmp copied to Dest. No tests.
    I3DMathEngine* Math = AcquireMathEngine();
    float Tmp[3];
    Math->TransposeTransform(*(FVector*)Tmp,
                                                          *(const FMatrix*)A,
                                                          *(const FVector*)B); // +0x38
    Dest[0] = Tmp[0];
    Dest[1] = Tmp[1];
    Dest[2] = Tmp[2];
}

// An IK joint id (JointIds[i], StartJoint) is an IEngine instance handle.
static void* IkJointInstance(int JointId)
{
    return (void*)((ptrdiff_t)JointId);
}

static void IkCopy3(float* Dest, const float* Src)
{
    Dest[0] = Src[0];
    Dest[1] = Src[1];
    Dest[2] = Src[2];
}

static void IkCopy9(float* Dest, const float* Src)
{
    int Index = 0;
    while (Index < 9)
    {
        Dest[Index] = Src[Index];
        Index += 1;
    }
}

static void IkCopy4(float* Dest, const float* Src)
{
    Dest[0] = Src[0];
    Dest[1] = Src[1];
    Dest[2] = Src[2];
    Dest[3] = Src[3];
}

// FUN_065fb480 (ECX = vec), 065fb480..b4a0: ((x*x + y*y) + z*z), FSQRT, returned in ST0 --
// a double, never rounded to float (until 2026-09-30 the port computed and returned float).
static double IkLen3(const float* Vec)
{
    const double X = (double)Vec[0];
    const double Y = (double)Vec[1];
    const double Z = (double)Vec[2];
    return std::sqrt(X * X + Y * Y + Z * Z);
}

// FUN_065fb4b0 (cdecl: Dest, A, B), 065fb4b0..b4ea: each component is a difference of two
// products formed on the FPU (double) and stored once as float (until 2026-09-30 the port
// rounded each product to float first).
static void IkCross3(float* Dest, const float* Left, const float* Right)
{
    const double Z = (double)Left[0] * Right[1] - (double)Left[1] * Right[0];
    const double Y = (double)Left[2] * Right[0] - (double)Right[2] * Left[0];
    const double X = (double)Left[1] * Right[2] - (double)Left[2] * Right[1];
    Dest[0] = (float)X;
    Dest[1] = (float)Y;
    Dest[2] = (float)Z;
}

// FUN_065fb4f0: columns → 3×3.
static void IkColsToMat(float* Out9, const float* Col0, const float* Col1, const float* Col2)
{
    Out9[0] = Col0[0];
    Out9[3] = Col0[1];
    Out9[6] = Col0[2];
    Out9[1] = Col1[0];
    Out9[4] = Col1[1];
    Out9[7] = Col1[2];
    Out9[2] = Col2[0];
    Out9[5] = Col2[1];
    Out9[8] = Col2[2];
}

// FUN_065fb100 (cdecl: Out, Dir, Parent 3x3, flags byte), 065fb100..b479, 0x54-byte frame.
static void IkLookAt(float* Out9, const float* Dir, const float* Parent9, unsigned char Flags)
{
    float Col0[3] = {0.0f, 0.0f, 0.0f};
    float Col1[3] = {0.0f, 0.0f, 0.0f};
    float Col2[3] = {0.0f, 0.0f, 0.0f};
    if ((Flags & 4) != 0)
    {
        IkCopy3(Col0, Dir);
        const double P2 = (double)Parent9[2];
        const double P5 = (double)Parent9[5];
        const double P8 = (double)Parent9[8];
        Col2[0] = (float)((double)Col0[2] * P5 - (double)Col0[1] * P8);
        Col2[1] = (float)(P8 * (double)Col0[0] - (double)Col0[2] * P2);
        Col2[2] = (float)((double)Col0[1] * P2 - P5 * (double)Col0[0]);
        const double Y0 = (double)Col2[0];
        const double Y1 = (double)Col2[1];
        const double Y2 = (double)Col2[2];
        const double Inv = 1.0 / std::sqrt(Y2 * Y2 + Y1 * Y1 + Y0 * Y0);
        Col1[0] = (float)(Y0 * Inv);
        Col1[1] = (float)(Y1 * Inv);
        Col1[2] = (float)(Y2 * Inv);
        IkCross3(Col2, Col0, Col1);
    }
    else if ((Flags & 8) != 0)
    {
        IkCopy3(Col1, Dir);
        float Temp[3];
        Temp[0] = Parent9[0];
        Temp[1] = Parent9[3];
        Temp[2] = Parent9[6];
        IkCross3(Col2, Temp, Col1);
        const double Inv = 1.0 / IkLen3(Col2);
        Col2[0] = (float)((double)Col2[0] * Inv);
        Col2[1] = (float)((double)Col2[1] * Inv);
        Col2[2] = (float)((double)Col2[2] * Inv);
        IkCross3(Col0, Col1, Col2);
    }
    else if ((Flags & 0x10) != 0)
    {
        IkCopy3(Col2, Dir);
        Col1[0] = Parent9[1];
        Col1[1] = Parent9[4];
        Col1[2] = Parent9[7];
        IkCross3(Col0, Col1, Col2);
        const double Inv = 1.0 / IkLen3(Col0);
        Col0[0] = (float)((double)Col0[0] * Inv);
        Col0[1] = (float)((double)Col0[1] * Inv);
        Col0[2] = (float)((double)Col0[2] * Inv);
        IkCross3(Col1, Col2, Col0);
    }
    IkColsToMat(Out9, Col0, Col1, Col2);
}

// FUN_065fbc90: quat → 2×log axis (acos / sin).
// DAT_066013b0: the double widening of 1e-5f (bytes 00000080b5f8e43e).
static const double G_IkLogEps = (double)1.0e-5f;
// DAT_06601348: the double 1e-5 (bytes f168e388b5f8e43e).
static const double G_IkExpEps = 1.0e-5;

// FUN_065fbc90 (cdecl: Out3, Quat wxyz), 065fbc90..bd21: FLD 1.0f, FSUB w, FABS, FCOMP double
static void IkQuatLogVec(float* Out3, const float* Quat)
{
    const double W = (double)Quat[0];
    if (std::fabs(1.0 - W) < G_IkLogEps)
    {
        Out3[0] = 0.0f;
        Out3[1] = 0.0f;
        Out3[2] = 0.0f;
        return;
    }
    const double Scale = std::acos(W) / std::sqrt(1.0 - W * W);
    Out3[0] = (float)(Scale * (double)Quat[1]);
    Out3[1] = (float)(Scale * (double)Quat[2]);
    Out3[2] = (float)(Scale * (double)Quat[3]);
}

// FUN_065fbd30 (cdecl: Out4 wxyz, Axis), 065fbd30..bde4: Len = sqrt((x*x + y*y) + z*z) inline
static void IkQuatExp(float* Out4, const float* Axis)
{
    const double Len = IkLen3(Axis);
    if (!(Len > G_IkExpEps))
    {
        Out4[0] = 1.0f;
        Out4[1] = 0.0f;
        Out4[2] = 0.0f;
        Out4[3] = 0.0f;
        return;
    }
    const float W = (float)(std::cos(Len));
    const double Scale = std::sin(Len) / Len;
    Out4[0] = W;
    Out4[1] = (float)(Scale * (double)Axis[0]);
    Out4[2] = (float)(Scale * (double)Axis[1]);
    Out4[3] = (float)(Scale * (double)Axis[2]);
}

// FUN_065fbdf0 (cdecl: Out3, A, B -- wxyz), 065fbdf0..bf35: dot = ((A3B3 + A2B2) + A1B1) + A0B0
static void IkQuatLog(float* Out3, const float* QuatA, const float* QuatB)
{
    const double Dot = (double)QuatA[3] * QuatB[3] +
                       (double)QuatA[2] * QuatB[2] +
                       (double)QuatA[1] * QuatB[1] +
                       (double)QuatA[0] * QuatB[0];
    float UseB[4];
    if (!(Dot >= 0.0))
    {
        UseB[0] = -QuatB[0];
        UseB[1] = -QuatB[1];
        UseB[2] = -QuatB[2];
        UseB[3] = -QuatB[3];
    }
    else
    {
        IkCopy4(UseB, QuatB);
    }
    float ConjA[4];
    ConjA[0] = QuatA[0];
    ConjA[1] = -QuatA[1];
    ConjA[2] = -QuatA[2];
    ConjA[3] = -QuatA[3];
    float Prod[4];
    AcquireMathEngine()->MulQuat(*(FQuaternion*)Prod,
                                 *(const FQuaternion*)UseB,
                                 *(const FQuaternion*)ConjA); // +0x18
    float Log3[3];
    IkQuatLogVec(Log3, Prod);
    Out3[0] = Log3[0] + Log3[0];
    Out3[1] = Log3[1] + Log3[1];
    Out3[2] = Log3[2] + Log3[2];
}

// The root-bone read retail inlines (065f41e4, 065f5017, 065f507c, ...): +8 (PartArray) null
// -> -1, else [[[+8]]+4]+4 -- PartArray is the only test.
static int FirstBoneInstance(FDeformInstance* Instance)
{
    if (Instance->PartArray == nullptr)
        return -1;
    return (int)(
        (ptrdiff_t)(Instance->PartArray[0].Lod->BoneSlots[0].Instance));
}

// FUN_065f5950, thiscall (the instance in ECX, never read) RET 0x4 (Node), 065f5950..a5:
static void StopAimNode(FIkListNode* Node)
{
    if (Node == (FIkListNode*)((ptrdiff_t)-1))
        return;
    Node->Event->Flags34 = (unsigned char)(Node->Event->Flags34 | 1);
    if (G_Animation == nullptr)
        return;
    int Cursor = Node->Event->EndJoint;
    while (Cursor != Node->Event->StartJoint)
    {
        G_Animation->HasAnyHandle((unsigned long)Cursor);
        Cursor = (int)((ptrdiff_t)(
            G_Engine->GetInstanceParent((void*)((ptrdiff_t)Cursor))));
    }
}

// DAT_066013a8 (PE bytes 00 00 00 36): the float 0x36000000 = 2^-19, written out exactly
// (the old 1.907349e-06f literal is not guaranteed to round to the same float).
static const float G_IkGimbalEps = 1.9073486328125e-06f;

// The Euler rebuild FUN_065f5400 (065f57e5..587c) and FUN_065fb900 inline with the same
static void IkEulerRebuild(float* Out9, float Yaw, float Pitch, float Roll)
{
    const double SinY = std::sin((double)Yaw);
    const double CosY = std::cos((double)Yaw);
    const double SinP = std::sin((double)Pitch);
    const double CosP = std::cos((double)Pitch);
    const double SinR = std::sin((double)Roll);
    const double CosR = std::cos((double)Roll);
    const double SinPSinY = SinP * SinY;
    const float SinRCosY = (float)(SinR * CosY);
    const float CosRCosY = (float)(CosR * CosY);
    Out9[0] = (float)(CosR * CosP);
    Out9[1] = (float)(SinPSinY * CosR - (double)SinRCosY);
    Out9[2] = (float)((double)CosRCosY * SinP + SinR * SinY);
    Out9[3] = (float)(SinR * CosP);
    Out9[4] = (float)(SinPSinY * SinR + (double)CosRCosY);
    Out9[5] = (float)((double)SinRCosY * SinP - CosR * SinY);
    Out9[6] = (float)-SinP;
    Out9[7] = (float)(CosP * SinY);
    Out9[8] = (float)(CosP * CosY);
}

// FUN_065fb090: count of yaw/pitch/roll outside [min,max] on Relative.
static int IkEulerOutOfRange(const float* Bone, float Yaw, float Pitch, float Roll)
{
    int Count = 0;
    if (Yaw < Bone[0x4C / 4] || Bone[0x50 / 4] < Yaw)
        Count += 1;
    if (Pitch < Bone[0x54 / 4] || Bone[0x58 / 4] < Pitch)
        Count += 1;
    if (Roll < Bone[0x40 / 4] || Bone[0x44 / 4] < Roll)
        Count += 1;
    return Count;
}

// FUN_065fbb90 (cdecl: OutYaw, OutPitch, OutRoll, Mat9, bAlt byte), 065fbb90..bc89: the
static void IkEulerExtract(float* OutYaw, float* OutPitch, float* OutRoll, const float* Mat9,
                           bool bAlt)
{
    const double M0 = (double)Mat9[0];
    const double M3 = (double)Mat9[3];
    const double M6 = (double)Mat9[6];
    const double Hyp = std::sqrt(M0 * M0 + M3 * M3);
    if (!(Hyp > (double)G_IkGimbalEps))
    {
        if (bAlt)
        {
            *OutYaw = (float)(std::atan2((double)Mat9[5], -(double)Mat9[4]));
            *OutPitch = (float)(std::atan2(M6, -Hyp));
        }
        else
        {
            *OutYaw = (float)(std::atan2(-(double)Mat9[5], (double)Mat9[4]));
            *OutPitch = (float)(std::atan2(-M6, Hyp));
        }
        *OutRoll = 0.0f;
        return;
    }
    if (bAlt)
    {
        *OutYaw = (float)(std::atan2((double)Mat9[7], -(double)Mat9[8]));
        *OutPitch = (float)(std::atan2(-M6, -Hyp));
        *OutRoll = (float)(std::atan2(M3, -M0));
        return;
    }
    *OutYaw = (float)(std::atan2((double)Mat9[7], (double)Mat9[8]));
    *OutPitch = (float)(std::atan2(-M6, Hyp));
    *OutRoll = (float)(std::atan2(M3, M0));
}

// FUN_065fb900 (cdecl: Out9, Bone, Mat9), 065fb900..bb86. Answers Out9 (EAX).
static float* IkEulerClamp(float* Out9, const float* Bone, const float* Mat9)
{
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float Roll = 0.0f;
    IkEulerExtract(&Yaw, &Pitch, &Roll, Mat9, false);
    const int Score = IkEulerOutOfRange(Bone, Yaw, Pitch, Roll);
    if (Score != 0)
    {
        float AltYaw = 0.0f;
        float AltPitch = 0.0f;
        float AltRoll = 0.0f;
        IkEulerExtract(&AltYaw, &AltPitch, &AltRoll, Mat9, true);
        if (!(IkEulerOutOfRange(Bone, AltYaw, AltPitch, AltRoll) > Score))
        {
            Yaw = AltYaw;
            Pitch = AltPitch;
            Roll = AltRoll;
        }
    }

    bool bChanged = false;
    if (Yaw < Bone[0x4C / 4])
    {
        Yaw = Bone[0x4C / 4];
        bChanged = true;
    }
    if (Yaw > Bone[0x50 / 4])
    {
        Yaw = Bone[0x50 / 4];
        bChanged = true;
    }
    if (Pitch < Bone[0x54 / 4])
    {
        Pitch = Bone[0x54 / 4];
        bChanged = true;
    }
    if (Pitch > Bone[0x58 / 4])
    {
        Pitch = Bone[0x58 / 4];
        bChanged = true;
    }
    if (Roll < Bone[0x40 / 4])
    {
        Roll = Bone[0x40 / 4];
        bChanged = true;
    }
    if (Roll > Bone[0x44 / 4])
    {
        Roll = Bone[0x44 / 4];
        bChanged = true;
    }

    float Local[9];
    if (!bChanged)
        IkCopy9(Local, Mat9);
    else
        IkEulerRebuild(Local, Yaw, Pitch, Roll);
    IkCopy9(Out9, Local);
    return Out9;
}

// Shared helpers for the two damped-least-squares solvers.

// FSvdMatrix-shaped vectors (FUN_065fafc0 writes its row count into the thirteenth dword).
struct FIkVec
{
    float Cell[12];
    int Count;
};

static_assert(sizeof(FIkVec) == 0x34, "IK vector is 13 dwords");

// Keep Rest9 adjacent: a locked first joint writes into the preceding cells.
struct FIkDlsJacobian
{
    float Rest9[9];
    FSvdMatrix J;
};

static_assert(offsetof(FIkDlsJacobian, J) == 0x24, "the Jacobian sits right after the scratch");

// The Jacobian's cells as the listing addresses them: flat, 12 per row, from Cell[0][0].
static float* IkDlsCells(FIkDlsJacobian* Block)
{
    return (float*)Block + 9;
}

// An unlocked joint's position rows (065fc4d9..c509 / 065fd2c1..d2f9), ESI = Cells + Col - 1:
// the skew matrix of the joint's lever arm (X, Y, Z) in columns Col..Col+2 of rows 0..2.
static void IkDlsWriteSkew(float* Cells, int Col, float AxisX, float AxisY, float AxisZ)
{
    Cells[Col] = 0.0f;
    Cells[Col + 1] = AxisZ;
    Cells[Col + 2] = -AxisY;
    Cells[Col + 12] = -AxisZ;
    Cells[Col + 13] = 0.0f;
    Cells[Col + 14] = AxisX;
    Cells[Col + 24] = AxisY;
    Cells[Col + 25] = -AxisX;
    Cells[Col + 26] = 0.0f;
}

// A locked joint's position rows (065fc4a3..c4d4 / 065fd285..d2bc): its skew terms are ADDED
// into the previous three columns (the joint moves with the last unlocked one). Each add is
// FLD / FADD(FSUB) float / FSTP float. No test of Col.
static void IkDlsAddSkew(float* Cells, int Col, float AxisX, float AxisY, float AxisZ)
{
    Cells[Col - 2] = AxisZ + Cells[Col - 2];
    Cells[Col - 1] = Cells[Col - 1] - AxisY;
    Cells[Col + 9] = Cells[Col + 9] - AxisZ;
    Cells[Col + 11] = AxisX + Cells[Col + 11];
    Cells[Col + 21] = AxisY + Cells[Col + 21];
    Cells[Col + 22] = Cells[Col + 22] - AxisX;
}

// An unlocked joint's orientation rows (065fc51e..c578 / 065fd30f..d35f): identity in the 3x3
// block at row PosDim, column Col (First = Col + 12 * PosDim).
static void IkDlsWriteIdentity(float* Cells, int First)
{
    Cells[First] = 1.0f;
    Cells[First + 1] = 0.0f;
    Cells[First + 2] = 0.0f;
    Cells[First + 12] = 0.0f;
    Cells[First + 13] = 1.0f;
    Cells[First + 14] = 0.0f;
    Cells[First + 24] = 0.0f;
    Cells[First + 25] = 0.0f;
    Cells[First + 26] = 1.0f;
}

// The damping (065fc5d1..c634 / 065fd3c2..d424): Lam2 = float(Damp38 * Damp38) (the product on
// the FPU, FSTP float); every cell of JtJ with row == column (rows and columns re-read from the
// matrix) becomes float(Lam2 + cell).
static void IkDlsDamp(FSvdMatrix* JtJ, float Damp)
{
    const float Lam2 = (float)((double)Damp * (double)Damp);
    int Row = 0;
    while (Row < JtJ->RowCount)
    {
        int Col = 0;
        while (Col < JtJ->ColCount)
        {
            if (Row == Col)
                JtJ->Cell[Row][Col] = (float)((double)Lam2 +
                                                         (double)JtJ->Cell[Row][Col]);
            Col += 1;
        }
        Row += 1;
    }
}

// The rest pull (065fc677..c792 / 065fd467..d58c), run when Flags28 bit 7 is clear: for each
static void IkDlsFillRestPull(FIkEvent* Event, int JointIters, float* Rest9, float* Pull)
{
    float* Cursor = Pull + 1;
    int Joint = 0;
    while (Joint < JointIters)
    {
        if (Event->LockBytes[Joint] == 0)
        {
            float Product[9];
            IkMathMulMatrix(Product, G_IkCurRot[Joint], Event->BindMats + Joint * 9);
            IkCopy9(Rest9, Product);
            float CurQ[4];
            IkMathMatrixToQuat(CurQ, G_IkCurRot[Joint + 1]);
            float RestQ[4];
            IkMathMatrixToQuat(RestQ, Rest9);
            float Log3[3];
            IkQuatLog(Log3, CurQ, RestQ);
            Cursor[-1] = Log3[0];
            Cursor[0] = Log3[1];
            Cursor[1] = Log3[2];
            Cursor += 3;
        }
        Joint += 1;
    }
}

// The trial step (065fc9ad..cb67 / 065fd7a8..d967): for each of the first JointIters joints,
static void IkDlsStepJoints(FIkEvent* Event, int JointIters, const float* Delta, float* Held3)
{
    const float* Cursor = Delta + 2;
    int Joint = 0;
    while (Joint < JointIters)
    {
        const float* Bone = (const float*)(
            G_Engine->GetInstanceRelative(IkJointInstance(Event->JointIds[Joint])));
        if (Event->LockBytes[Joint] == 0)
        {
            Held3[0] = Cursor[-2];
            Held3[1] = Cursor[-1];
            Held3[2] = Cursor[0];
            Cursor += 3;
        }
        float Axis[3];
        Axis[0] = (float)((double)Held3[0] * 0.5);
        Axis[1] = (float)((double)Held3[1] * 0.5);
        Axis[2] = (float)((double)Held3[2] * 0.5);
        float StepQ[4];
        IkQuatExp(StepQ, Axis);
        float StepM[9];
        IkMathQuatToMatrix(StepM, StepQ);
        float Rotated[9];
        IkMathMulMatrix(Rotated, StepM, G_IkCurRot[Joint + 1]);
        IkCopy9(G_IkNewRot[Joint + 1], Rotated);
        float Arm[3];
        IkMathTransform(Arm, G_IkNewRot[Joint], Bone + 1);
        G_IkNewPos[Joint + 1][0] = (float)((double)Arm[0] +
                                                      (double)G_IkNewPos[Joint][0]);
        G_IkNewPos[Joint + 1][1] = (float)((double)Arm[1] +
                                                      (double)G_IkNewPos[Joint][1]);
        G_IkNewPos[Joint + 1][2] = (float)((double)Arm[2] +
                                                      (double)G_IkNewPos[Joint][2]);
        Joint += 1;
    }
}

// The squared residual (065fcc4f..cc79 / 065fda4f..da79): 0.0 (DAT_06601318) on the FPU, then
// for i < TaskDim Sum += (Error_i - Achieved_i)^2 with the difference and its square in double.
static double IkDlsResidual(const FIkVec* Error, const FIkVec* Achieved, int TaskDim)
{
    double Sum = 0.0;
    int Index = 0;
    while (Index < TaskDim)
    {
        const double Diff = (double)Error->Cell[Index] -
                            (double)Achieved->Cell[Index];
        Sum = Sum + Diff * Diff;
        Index += 1;
    }
    return Sum;
}

// The write-back (065fcd10..ce43 / 065fdb10..dc43): for each of the first JointIters joints
// whose lock byte is clear, the local rotation transpose(NewRot[i]) x NewRot[i + 1] (math
// +0x20 into a temporary, copied, then math +0x50) goes to Quats[i].
static void IkDlsWriteQuats(FIkEvent* Event, int JointIters)
{
    int Joint = 0;
    while (Joint < JointIters)
    {
        if (Event->LockBytes[Joint] == 0)
        {
            const float* Parent = G_IkNewRot[Joint];
            float ParentT[9];
            ParentT[0] = Parent[0];
            ParentT[1] = Parent[3];
            ParentT[2] = Parent[6];
            ParentT[3] = Parent[1];
            ParentT[4] = Parent[4];
            ParentT[5] = Parent[7];
            ParentT[6] = Parent[2];
            ParentT[7] = Parent[5];
            ParentT[8] = Parent[8];
            float Product[9];
            IkMathMulMatrix(Product, ParentT, G_IkNewRot[Joint + 1]);
            float Local[9];
            IkCopy9(Local, Product);
            float Q[4];
            IkMathMatrixToQuat(Q, Local);
            IkCopy4(Event->Quats + Joint * 4, Q);
        }
        Joint += 1;
    }
}

// FUN_065fb030, thiscall (Event) RET 0x4 (JointCount), 065fb030..8c. A 0x2000-byte stack
static bool SetIkJointCount(FIkEvent* Event, unsigned int JointCount)
{
    if (JointCount > (unsigned int)G_IkMaxJoints)
    {
        char Message[0x2000];
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformIk, 0x15,
              FormatDeformMessage(Message, "too many joints specified for ik event. %d / %d",
                                  (int)JointCount, G_IkMaxJoints));
        return false;
    }
    Event->JointCount = (int)JointCount;
    return true;
}

static int DACOM_API IkApply(FIkEvent* Event, float* OutQuat, int MatchId, FIkApplyGate* Gate,
                             float Time);

// LAB_065fb540, the IK event's only table slot (06601340): EngBase's FUN_0661b420 calls it on
// the channel owner with (sampled value, channel key, bind descriptor, time).
int DACOM_API FIkEvent::Apply(float* Sampled, unsigned long ChannelKey, void* BindDesc, float Time)
{
    return IkApply(this, Sampled, (int)ChannelKey,
                   (FIkApplyGate*)BindDesc, Time);
}

// FUN_065fbf40 (thiscall, RET 0), 065fbf40..bfb5: vtable = 06601340 (the compiler's own reset
FIkEvent::~FIkEvent()
{
    int Index = 0;
    while (Index < JointCount)
    {
        G_Channel->Destroy(
            (unsigned long)((ptrdiff_t)Channels[Index]));
        Index += 1;
    }
    JointCount = 0;
    delete[] (unsigned char*)JointIds;
    JointIds = nullptr;
    delete[] (unsigned char*)Channels;
    Channels = nullptr;
    delete[] (unsigned char*)Quats;
    Quats = nullptr;
    delete[] (unsigned char*)BindMats;
    BindMats = nullptr;
    delete[] LockBytes;
    LockBytes = nullptr;
}


static void LinkListNode(FDeformInstance* Instance, FIkListNode* Node)
{
    FIkListNode* OldTail = Instance->ListTail;
    Instance->ListTail = Node;
    if (OldTail == nullptr)
        Instance->ListHead = Node;
    else
        OldTail->Next = Node;
    Node->Prev = OldTail;
    Node->Next = nullptr;
    Instance->ListCount += 1;
}

static void UnlinkListNode(FDeformInstance* Instance, FIkListNode* Node)
{
    if (Node->Prev == nullptr)
        Instance->ListHead = Node->Next;
    else
        Node->Prev->Next = Node->Next;
    if (Node->Next == nullptr)
        Instance->ListTail = Node->Prev;
    else
        Node->Next->Prev = Node->Prev;
    Instance->ListCount -= 1;
}

// The payload teardown every list walker inlines (065f4c9d, 065f4d60, 065f4e2d): Active != 0
// -> Event non-null -> FUN_065fbf40 + operator delete, +4 = 0; Active == 0 -> IAnimation2
// +0x28 (DestroyScript) on +4 with no test, +4 = -1.
static void DestroyNodePayload(FIkListNode* Node)
{
    if (Node->Active != 0)
    {
        FIkEvent* Event = Node->Event;
        if (Event != nullptr)
            delete Event;                                        // FUN_065fbf40 + operator delete
        Node->Event = nullptr;
        return;
    }
    G_Animation->DestroyScript(Node->Event);
    Node->Event = (FIkEvent*)((ptrdiff_t)-1);
}

// ... followed by the inlined unlink (prev/next fix-ups, +0x14 decrement) and operator delete.
static void DeleteListNode(FDeformInstance* Instance, FIkListNode* Node)
{
    DestroyNodePayload(Node);
    UnlinkListNode(Instance, Node);
    delete Node;
}

// FUN_065f4c80 (thiscall, RET 0), 065f4c80..4d20: from +0xC, next = +0x10 taken first; each
// node: payload teardown, unlink, delete. No test of the instance.
static void DestroyAllList(FDeformInstance* Instance)
{
    FIkListNode* Node = Instance->ListHead;
    while (Node != nullptr)
    {
        FIkListNode* Next = Node->Next;
        DeleteListNode(Instance, Node);
        Node = Next;
    }
}

// FUN_065f4d30 (thiscall, RET 4: Handle), 065f4d30..4dd8: the first node whose +4 equals
// Handle gets the payload teardown, unlink and delete; then return. No match -> nothing.
static void DestroyListByHandle(FDeformInstance* Instance, int Handle)
{
    FIkListNode* Node = Instance->ListHead;
    while (Node != nullptr)
    {
        FIkListNode* Next = Node->Next;
        const int Stored = (int)((ptrdiff_t)Node->Event);
        if (Stored == Handle)
        {
            DeleteListNode(Instance, Node);
            return;
        }
        Node = Next;
    }
}

// FUN_065f4de0 (thiscall, RET 0), 065f4de0..4ef9. Per node (next taken first):
static void SweepExpiredList(FDeformInstance* Instance)
{
    FIkListNode* Node = Instance->ListHead;
    while (Node != nullptr)
    {
        FIkListNode* Next = Node->Next;
        bool bDrop = false;
        if (Node->Active != 0)
        {
            FIkEvent* Event = Node->Event;
            if ((Event->Flags34 & 1) != 0 &&
                (unsigned char)(G_Channel->InMixer((unsigned long)(
                    (ptrdiff_t)Event->Channels[0]))) == 0)
                bDrop = true;
        }
        else if (G_Animation->FilterObjects(Node->Event) != 0)
            bDrop = true;
        if (bDrop)
            DeleteListNode(Instance, Node);
        Node = Next;
    }
    if (Instance->ListCount == 0)
    {
        Instance->Offset24 = Instance->PartArray[0].Lod->BoneSlots[0].LocalTransform[10] - Instance->Offset20;
    }
}

// FUN_065f6930: Current = Meshes[lod] or null; LodIndex = lod; toggle Flags bit0.
static bool SelectPartLod(FDeformPart* Part, int LodIndex)
{
    FDeformMesh* Old = Part->Current;
    if (LodIndex < 0 || Part->MeshCount <= LodIndex)
    {
        Part->Current = nullptr;
        Part->LodIndex = LodIndex;
        return Old != Part->Current;
    }
    FDeformMesh* Next = Part->Meshes[LodIndex];
    Part->Current = Next;
    if ((Part->Flags & 1) == 0 && Old == Next)
    {
        Part->Flags = (unsigned char)(Part->Flags & (unsigned char)(~1));
        Part->LodIndex = LodIndex;
        return Old != Part->Current;
    }
    // 065f6978..6989: MOV EDX,1; XOR BL,DL; AND BL,DL; XOR BL,AL -- bit 0 becomes 1 whatever
    // it was (until 2026-09-30 the port toggled it, clearing a set bit).
    Part->Flags = (unsigned char)(Part->Flags | 1);
    Part->LodIndex = LodIndex;
    return Old != Part->Current;
}

// FUN_065f69c0
// FUN_065f69c0 (thiscall, RET 4 -- the argument is never read): OR byte [this+0x30],2; AL=1.
static void MarkPartSkinDirty(FDeformPart* Part)
{
    Part->Flags = (unsigned char)(Part->Flags | 2);
}

// FUN_065f69d0 (thiscall on the part, RET 8: Camera, Unused), 065f69d0..6ac2. The second
// select_ib a base vertex of 0, and FDUMPed two port-only messages for null interfaces.
static unsigned char DrawPartLive(FDeformPart* Part, ICamera* Camera, void* /*Unused*/)
{
    if (Part->Current == nullptr)
        return 1;
    const bool bVbSelected = G_VertexBuffer->SelectVb(Part->Vb.Listing.VertexBuffer) >= 0;
    const unsigned char Flags = Part->Flags;
    const unsigned char bRebuild = ((Flags & 1) != 0 || !bVbSelected) ? 1 : 0;
    if ((Flags & 2) != 0 || bRebuild != 0)
    {
        if (Part->Current->Skin(Part->Lod, &Part->Vb, bRebuild) == 0)
            return 0;
        if (G_VertexBuffer->SelectVb(Part->Vb.Listing.VertexBuffer) < 0)
            return 0;
    }
    const bool bIbSelected =
        G_IndexBuffer->SelectIb(Part->Ib.Scratch.IndexBuffer,
                                (unsigned int)(Part->Vb.Listing.BaseVertex), 0, 0) >= 0;
    if ((Part->Flags & 1) != 0 || !bIbSelected)
    {
        if (Part->Current->FillIndexBuffer(&Part->Ib) == 0)
            return 0;
        if (G_IndexBuffer->SelectIb(Part->Ib.Scratch.IndexBuffer,
                                    (unsigned int)(Part->Vb.Listing.BaseVertex), 0,
                                    0) < 0)
            return 0;
    }
    Part->Current->Draw(Camera, Part);
    Part->Flags = (unsigned char)(Part->Flags & 0xFC);
    return 1;
}

static void WriteVec3(float* Dest, float X, float Y, float Z);
static void ScaleVec3(float* Out, const float* In, float Scale);

// FUN_065f45d0 (thiscall on the quaternion {w,x,y,z}, RET 8: Axis, Angle), 065f45d0..464f:
static void AxisAngleToQuat(float* Quat, const float* Axis, float Angle)
{
    const double Half = (double)Angle * 0.5;
    const double Length = std::sqrt((double)Axis[0] * Axis[0] +
                                    (double)Axis[1] * Axis[1] +
                                    (double)Axis[2] * Axis[2]);
    const float Scale = (float)(std::sin(Half) / Length);
    Quat[0] = (float)(std::cos(Half));
    Quat[1] = Scale * Axis[0];
    Quat[2] = Scale * Axis[1];
    Quat[3] = Scale * Axis[2];
}

// FUN_065f41d0 (thiscall on the instance, RET 0x24: P1..P9 = Name, Extra, ArgC, ArgD, ArgE,
static int PlayInstanceAnim(FDeformInstance* Instance, void* Name, void* Extra, void* ArgC, void* ArgD,
                            void* ArgE, void* ArgF, void* ArgG, void* ArgH, void* ScriptOrNull)
{
    SweepExpiredList(Instance);
    int BoneHandle = -1;
    if (Instance->PartArray != nullptr)
        BoneHandle = (int)(
            (ptrdiff_t)(Instance->PartArray[0].Lod->BoneSlots[0].Instance));
    void* Script = ScriptOrNull;
    if (Script == nullptr)
        Script = Instance->UserAt1C;
    const int Handle = (int)(G_Animation->PlayScript(
        Name, AsOpaqueDword(BoneHandle), (const char*)Extra,
        (IChannelSink*)Instance, Script));
    if (Handle == -1)
        return -1;

    FBoneRuntime* Bone = Instance->PartArray[0].Lod->BoneSlots;
    const float* BonePos =
        Bone->LocalTransform + 9;
    float Vec[3];
    Vec[0] = 0.0f;
    Vec[1] = 0.0f;
    Vec[2] = 0.0f;
    const float ArgCFloat = FloatFromOpaqueDword(ArgC);
    if (ArgCFloat > 0.0f)
    {
        IRootData* Root = nullptr;
        if (G_Animation->QueryInterface(Iids::RootData, (void**)&Root) == 0)
        {
            // 065f42af..42dc: the pushes, last first, are 1.0f, ArgF, ArgD, ArgE, ArgC, ArgG,
            // &Vec, Handle.
            const unsigned char bTouched = Root->Eval(
                (unsigned long)Handle, Vec,
                (unsigned long)((ptrdiff_t)ArgG), ArgCFloat,
                FloatFromOpaqueDword(ArgE), ArgD, ArgF, 1.0f);
            if (bTouched != 0)
            {
                Vec[2] = 0.0f;
                Vec[0] = 0.0f;
            }
        }
        if (Root != nullptr)
            Root->Release();
    }
    float DurationOut = 0.0f;
    float Y = 0.0f;
    if (G_Animation->GetNamedDurationOut(Name, (const char*)Extra, &DurationOut) != 0)
        Y = DurationOut + Instance->Offset20;
    else
        Y = Instance->Offset24 + Instance->Offset20;
    float Pos[3];
    Pos[0] = BonePos[0] + Vec[0];
    Pos[1] = Vec[1] + Y;
    Pos[2] = BonePos[2] + Vec[2];

    float Quat[4];
    float UpAxis[3];
    const float Twist = FloatFromOpaqueDword(ArgH);
    if (Twist < 0.0f)
    {
        float Row[3];
        WriteVec3(Row, Bone->LocalTransform[2], Bone->LocalTransform[5], Bone->LocalTransform[8]);
        const float Angle = (float)(
            std::atan2((double)Row[0], (double)Row[2]));
        WriteVec3(UpAxis, 0.0f, 1.0f, 0.0f);
        AxisAngleToQuat(Quat, UpAxis, Angle);
    }
    else
    {
        const double Half = (double)Twist * 0.5;
        WriteVec3(UpAxis, 0.0f, 1.0f, 0.0f);
        Quat[0] = (float)(std::cos(Half));
        const float Scale = (float)(std::sin(Half) / std::sqrt(1.0));
        ScaleVec3(&Quat[1], UpAxis, Scale);
    }

    float Xform[12];
    I3DMathEngine* Math = AcquireMathEngine();
    Math->QuaternionToMatrix(*(FMatrix*)Xform,
                             *(const FQuaternion*)Quat); // +0x54
    Xform[9] = Pos[0];
    Xform[10] = Pos[1];
    Xform[11] = Pos[2];
    const long Applied = (long)(G_Animation->EvalPlayback(
        (unsigned long)Handle,
        (unsigned long)((ptrdiff_t)ArgG), ArgCFloat,
        FloatFromOpaqueDword(ArgE), ArgD, ArgF, AsOpaqueDword(1.0f), (void*)Xform));
    if (Applied < 0)
    {
        G_Animation->DestroyScript((void*)((ptrdiff_t)Handle));
        return -1;
    }

    FIkListNode* Node = new FIkListNode;
    Node->Active = 0;
    LinkListNode(Instance, Node);
    Node->Event = (FIkEvent*)((ptrdiff_t)Handle);
    std::memcpy(&Node->Extra8, &Y, sizeof(float));
    return Handle;
}

// FUN_065f4580 thiscall RET 0xC: [this]=p1, [this+4]=p2, [this+8]=p3.
static void WriteVec3(float* Dest, float X, float Y, float Z)
{
    Dest[0] = X;
    Dest[1] = Y;
    Dest[2] = Z;
}

// FUN_065f45a0 cdecl: out = in * s.
static void ScaleVec3(float* Out, const float* In, float Scale)
{
    Out[0] = In[0] * Scale;
    Out[1] = In[1] * Scale;
    Out[2] = In[2] * Scale;
}

// FUN_065f4c50 cdecl: out = a - b.
static void SubVec3(float* Out, const float* A, const float* B)
{
    Out[0] = A[0] - B[0];
    Out[1] = A[1] - B[1];
    Out[2] = A[2] - B[2];
}

// FUN_065f4660 thiscall RET 0x24 (Name, Extra, Time, ArgD, ArgE, ArgF, ArgG, ArgH, Joint),
static int PlayInstanceAnimChain(FDeformInstance* Instance, void* Name, void* Extra, void* ArgC,
                                 void* ArgD, void* ArgE, void* ArgF, void* ArgG, void* ArgH,
                                 void* Joint)
{
    const float* const JointPos = G_Engine->GetInstancePosition(Joint);
    float StartPos[3];
    StartPos[0] = JointPos[0];
    StartPos[1] = JointPos[1];
    StartPos[2] = JointPos[2];

    const int PlayHandle =
        PlayInstanceAnim(Instance, Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, nullptr);
    const unsigned long Play = (unsigned long)PlayHandle;
    const float Time = FloatFromOpaqueDword(ArgC);

    void* const Stop = (void*)((ptrdiff_t)(FirstBoneInstance(Instance)));

    // EngBase FAnimBindOut {Kind, Extra, Typed, Instance}; +0x10 of the frame is outside it.
    unsigned long Query[4];
    Query[0] = 0;
    Query[1] = 0;
    Query[2] = 0;
    Query[3] = 0xFFFFFFFF;

    std::vector<const unsigned char*> Relatives;
    std::vector<FQuaternion> ChainQuats;
    void* Cursor = Joint;
    while (Cursor != Stop)
    {
        void* const Parent = G_Engine->GetInstanceParent(Cursor);
        Relatives.push_back(
            (const unsigned char*)(G_Engine->GetInstanceRelative(Cursor)));
        Query[3] = (unsigned long)((ptrdiff_t)Cursor);
        const unsigned long Channel = G_Animation->FindObject(Play, Query);
        IChannelArchetype* Archetype = nullptr;
        G_Channel->GetObject(Channel, (void**)(&Archetype));
        unsigned long Flags = 0;
        Archetype->GetFlags(&Flags);
        const unsigned char* KeyA = nullptr;
        const unsigned char* KeyB = nullptr;
        float Alpha = 0.0f;
        Archetype->Sample(Time, &KeyA, &KeyB, &Alpha);
        Archetype->Release();
        FQuaternion Quat;
        AcquireMathEngine()->QuatNlerp(Quat, *(const FQuaternion*)KeyA,
                                       *(const FQuaternion*)KeyB, Alpha);
        ChainQuats.push_back(Quat);
        Cursor = Parent;
    }
    const int ChainCount = (int)(ChainQuats.size());

    Query[3] = (unsigned long)((ptrdiff_t)Stop);
    Query[0] = 1;
    const unsigned long RootChannel = G_Animation->FindObject(Play, Query);
    IChannelArchetype* RootArchetype = nullptr;
    G_Channel->GetObject(RootChannel, (void**)(&RootArchetype));
    unsigned long RootFlags = 0;
    RootArchetype->GetFlags(&RootFlags);
    const unsigned char* RootA = nullptr;
    const unsigned char* RootB = nullptr;
    float RootAlpha = 0.0f;
    RootArchetype->Sample(Time, &RootA, &RootB, &RootAlpha);
    const float* const PosA = (const float*)RootA;
    const float* const PosB = (const float*)RootB;

    float Diff[3];
    SubVec3(Diff, PosB, PosA);
    float Tail[3];
    ScaleVec3(Tail, Diff, RootAlpha);
    float Accum[3];
    WriteVec3(Accum, Tail[0] + PosA[0], Tail[1] + PosA[1], Tail[2] + PosA[2]);

    FQuaternion RootQuat;
    AcquireMathEngine()->QuatNlerp(RootQuat, *(const FQuaternion*)(RootA + 0xC),
                                   *(const FQuaternion*)(RootB + 0xC), RootAlpha);
    RootArchetype->Release();

    FMatrix Rot;
    AcquireMathEngine()->QuaternionToMatrix(Rot, RootQuat);
    FMatrix Running = Rot;
    int Index = ChainCount - 1;
    while (Index >= 0)
    {
        FMatrix Step;
        AcquireMathEngine()->QuaternionToMatrix(Step, ChainQuats[Index]);
        const unsigned char* const Relative = Relatives[Index];
        FMatrix Mul;
        AcquireMathEngine()->MulMatrix(Mul, Running, Step);
        AcquireMathEngine()->MulMatrix(Rot, Mul,
                                       *(const FMatrix*)(Relative + 0x1C));
        FVector XfA;
        AcquireMathEngine()->TransformMatrix(XfA, Rot,
                                             *(const FVector*)(Relative + 0x10));
        FVector XfB;
        AcquireMathEngine()->TransformMatrix(XfB, Running,
                                             *(const FVector*)(Relative + 4));
        const float SumZ =
            (float)((double)XfB.Z + (double)Accum[2]);
        const float X = (float)(
            (double)XfB.X + (double)Accum[0] - (double)XfA.X);
        const float Y = (float)(
            (double)XfB.Y + (double)Accum[1] - (double)XfA.Y);
        const float Z = (float)((double)SumZ - (double)XfA.Z);
        Accum[0] = X;
        Accum[1] = Y;
        Accum[2] = Z;
        Tail[0] = X;
        Tail[1] = Y;
        Tail[2] = Z;
        Running = Rot;
        Index -= 1;
    }

    const float* const BonePos = Instance->PartArray[0].Lod->BoneSlots[0].LocalTransform + 9;
    const float DiffZ = (float)((double)StartPos[2] - BonePos[2]);
    float Delta[3];
    Delta[0] = (float)((double)StartPos[0] - BonePos[0] -
                                  (double)Tail[0]);
    Delta[1] = (float)((double)StartPos[1] - BonePos[1] -
                                  (double)Tail[1]);
    Delta[2] = (float)((double)DiffZ - (double)Tail[2]);
    G_Animation->AddOverlayPos(Play, Delta);
    return PlayHandle;
}

// The locals both solvers share, by their frame slots (FUN_065fc000 / FUN_065fce60). Nothing
// in the frame is cleared: J, the vectors, TargetPos / TargetMat when their flag is clear and
// Held3 before the first unlocked joint hold whatever was there, as in retail.
struct FIkDlsFrame
{
    bool bPos;               // [0x13] / [0x13]  Flags28 bit 5
    bool bOri;               // [0x11] / [0x12]  Flags28 bit 6
    int PosDim;              // [0x48] / [0x54]  bPos ? 3 : 0
    int TaskDim;             // [0x58] / [0x58]  PosDim + (bOri ? 3 : 0)
    int Dof;                 // [0x38] / [0x40]
    int JointIters;          // [0x30] / [0x44]
    int EndIndex;            // [0x64] / [0x70]  JointCount, the chain end's scratch row
    int Counter;             // [0x24] / [0x20]
    int CounterFloor;        // [0xa8] / [0x9c]
    float Alpha;             // [0x20] / [0x28]
    float Accum;             // [0x1c] / [0x24]
    float TargetPos[3];      // [0x68] / [0x5c]  TargetA, copied when bPos
    float TargetMat[9];      // [0x2d0] / [0x2c4] TargetB, copied when bOri
    float TargetQ[4];        // [0x124] / [0x118]
    float Held3[3];          // [0xac] / [0xac]
    FIkDlsJacobian Block;    // [0x2f4] / [0x2e8]
    FSvdMatrix JtJ;          // [0x560] / [0x554]
    FSvdMatrix Jt;           // [0x7a8] / [0x79c]
    FSvdMatrix Pinv;         // [0x9f0] / [0x9e4]
    FIkVec Error;            // [0x134] / [0x128]
    FIkVec Achieved;         // [0x168] / [0x15c]
    FIkVec JtE;              // [0x19c] / [0x190]
    FIkVec Delta;            // [0x268] / [0x25c]
    FIkVec Pull;             // [0x29c] / [0x290]
};

// The entry reads of the chain's start (065fc091..c0fc / 065fceda..cf45): IEngine +0x90
// (StartJoint) -> NewPos[0] and CurPos[0]; IEngine +0x98 (StartJoint) -> NewRot[0] (9 dwords),
// then NewRot[0] -> CurRot[0].
static void IkDlsReadStart(FIkEvent* Event)
{
    const float* StartPos = G_Engine->GetInstancePosition(IkJointInstance(Event->StartJoint));
    IkCopy3(G_IkNewPos[0], StartPos);
    IkCopy3(G_IkCurPos[0], StartPos);
    IkCopy9(G_IkNewRot[0], G_Engine->GetInstanceOrientation(IkJointInstance(Event->StartJoint)));
    IkCopy9(G_IkCurRot[0], G_IkNewRot[0]);
}

// Whether the chain end's orientation is off target (065fc2aa..c31d / 065fd0f9..d16c), asked
static bool IkDlsOriOff(const FIkDlsFrame* Frame)
{
    float CurQ[4];
    IkMathMatrixToQuat(CurQ, G_IkCurRot[Frame->EndIndex]);
    float Log3[3];
    IkQuatLog(Log3, CurQ, Frame->TargetQ);
    const double X = (double)Log3[0];
    const double Y = (double)Log3[1];
    const double Z = (double)Log3[2];
    return std::sqrt((Y * Y + Z * Z) + X * X) > 0.01;
}

// The dimension stores before the first round (065fc323..c3a6 / 065fd172..d1e3). The J
// counts are the only ones that are read as stored; FUN_065f9d70, FUN_065faef0 and
// FUN_065fafc0 rewrite the others.
static void IkDlsSetup(FIkDlsFrame* Frame)
{
    Frame->Block.J.RowCount = Frame->TaskDim;
    Frame->Block.J.ColCount = Frame->Dof;
    Frame->Jt.RowCount = Frame->Dof;
    Frame->Jt.ColCount = Frame->TaskDim;
    Frame->JtJ.RowCount = Frame->TaskDim + Frame->Dof;
    Frame->JtJ.ColCount = Frame->Dof;
    Frame->Delta.Count = Frame->Dof;
    Frame->Achieved.Count = Frame->TaskDim;
    Frame->Counter = 1;
    Frame->CounterFloor = 1;
    Frame->Alpha = 1.0f;
    Frame->Accum = 0.0f;
}

// One round after the Jacobian is built (065fc5a0..cd0a / 065fd391..db0a). Answers whether
static bool IkDlsRound(FIkDlsFrame* Frame, FIkEvent* Event)
{
    SvdTranspose(&Frame->Block.J, &Frame->Jt);
    SvdMulAB(&Frame->Jt, &Frame->Block.J, &Frame->JtJ);
    IkDlsDamp(&Frame->JtJ, Event->Damp38);
    Frame->Pinv.RowCount = Frame->JtJ.ColCount;
    Frame->Pinv.ColCount = Frame->JtJ.RowCount;
    SvdPseudoInverse(&Frame->JtJ, &Frame->Pinv);

    const bool bPull = ((Event->Flags28 >> 7) & 1u) == 0;
    Frame->Pull.Count = Frame->Dof;
    if (bPull)
        IkDlsFillRestPull(Event, Frame->JointIters, Frame->Block.Rest9, Frame->Pull.Cell);

    float Step = 2.0f;
    float PrevErr = FLT_MAX;
    float ErrDelta = 0.0f;
    for (;;)
    {
        Step = (float)((double)Step * 0.5);
        const float* const End = G_IkCurPos[Frame->EndIndex];
        float Diff[3];
        Diff[0] = (float)((double)Frame->TargetPos[0] - (double)End[0]);
        Diff[1] = (float)((double)Frame->TargetPos[1] - (double)End[1]);
        Diff[2] = (float)((double)Frame->TargetPos[2] - (double)End[2]);
        const int Divisor = Frame->Counter >= 1 ? Frame->Counter : Frame->CounterFloor;
        const double Scale = 1.0 / (double)Divisor;
        Frame->Counter -= 1;
        Frame->Error.Count = Frame->TaskDim;
        const float ScaledX = (float)((double)Diff[0] * Scale);
        const float ScaledY = (float)((double)Diff[1] * Scale);
        const double ScaledZ = Scale * (double)Diff[2];
        if (Frame->bPos)
        {
            Frame->Error.Cell[0] = ScaledX;
            Frame->Error.Cell[1] = ScaledY;
            Frame->Error.Cell[2] = (float)ScaledZ;
        }
        if (Frame->bOri)
        {
            float CurQ[4];
            IkMathMatrixToQuat(CurQ, G_IkCurRot[Frame->EndIndex]);
            float Log3[3];
            IkQuatLog(Log3, CurQ, Frame->TargetQ);
            Frame->Error.Cell[Frame->PosDim] = Log3[0];
            Frame->Error.Cell[Frame->PosDim + 1] = Log3[1];
            Frame->Error.Cell[Frame->PosDim + 2] = Log3[2];
        }

        Frame->JtE.Count = Frame->Error.Count;
        SvdMulVec(&Frame->Jt, Frame->Error.Cell, Frame->JtE.Cell);
        if (bPull)
        {
            const float Gain = (float)((double)Event->Damp38 * 0.25);
            int Index = 0;
            while (Index < Frame->JtE.Count)
            {
                Frame->JtE.Cell[Index] = (float)(
                    (double)Gain * (double)(Frame->Pull.Cell[Index]) +
                    (double)(Frame->JtE.Cell[Index]));
                Index += 1;
            }
        }
        SvdMulVec(&Frame->Pinv, Frame->JtE.Cell, Frame->Delta.Cell);

        float OldEnd[3];
        IkCopy3(OldEnd, End);
        IkDlsStepJoints(Event, Frame->JointIters, Frame->Delta.Cell, Frame->Held3);
        if (Frame->bPos)
        {
            const float* const NewEnd = G_IkNewPos[Frame->EndIndex];
            Frame->Achieved.Cell[0] = (float)((double)NewEnd[0] - (double)OldEnd[0]);
            Frame->Achieved.Cell[1] = (float)((double)NewEnd[1] - (double)OldEnd[1]);
            Frame->Achieved.Cell[2] = (float)((double)NewEnd[2] - (double)OldEnd[2]);
        }
        if (Frame->bOri)
        {
            float NewQ[4];
            IkMathMatrixToQuat(NewQ, G_IkNewRot[Frame->EndIndex]);
            float Log3[3];
            IkQuatLog(Log3, NewQ, Frame->TargetQ);
            Frame->Achieved.Cell[Frame->PosDim] = Log3[0];
            Frame->Achieved.Cell[Frame->PosDim + 1] = Log3[1];
            Frame->Achieved.Cell[Frame->PosDim + 2] = Log3[2];
        }

        const double Sum = IkDlsResidual(&Frame->Error, &Frame->Achieved, Frame->TaskDim);
        ErrDelta = (float)(Sum - (double)PrevErr);
        PrevErr = (float)Sum;
        if (!(Sum > 0.01))
            break;
        if (!((double)ErrDelta < -1.0e-5))
            break;
    }

    Frame->Accum = (float)((double)Step * (double)Frame->Alpha +
                                      (double)Frame->Accum);
    const double Remain = 1.0 - (double)Frame->Accum;
    const float RemainF = (float)Remain;
    if (Remain < (double)Frame->Alpha)
        Frame->Alpha = RemainF;
    if (std::fabs((double)Frame->Accum - 1.0) <= 0.01)
        return false;
    return !(ErrDelta >= 0.0f);
}

// FUN_065fc000 (thiscall on the event, RET 0; LAB_065fb540 calls it for a chain of two or more
static void IkSolveDls(FIkEvent* Event)
{
    FIkDlsFrame Frame;
    const unsigned long Flags = Event->Flags28;
    Frame.bPos = ((Flags >> 5) & 1u) != 0;
    Frame.bOri = ((Flags >> 6) & 1u) != 0;
    const int Count = Event->JointCount;
    Frame.Dof = (Frame.bOri ? Count : Count - 1) * 3;
    Frame.PosDim = Frame.bPos ? 3 : 0;
    Frame.TaskDim = Frame.PosDim + (Frame.bOri ? 3 : 0);
    if (Frame.bOri)
        Frame.Dof += Event->Extra30 * -3;
    else if (Frame.bPos)
    {
        int Locked = Event->Extra30;
        if (Locked > 0 && Event->LockBytes[Count - 1] != 0)
            Locked -= 1;
        Frame.Dof += Locked * -3;
    }

    IkDlsReadStart(Event);
    int Joint = 0;
    while (Joint < Event->JointCount)
    {
        G_Engine->GetJointParams(IkJointInstance(Event->JointIds[Joint]), 0, Event->Quats + Joint * 4);
        G_Engine->GetInstanceRelative(IkJointInstance(Event->JointIds[Joint]));
        IkCopy3(G_IkCurPos[Joint + 1],
                G_Engine->GetInstancePosition(IkJointInstance(Event->JointIds[Joint])));
        IkCopy9(G_IkCurRot[Joint + 1],
                G_Engine->GetInstanceOrientation(IkJointInstance(Event->JointIds[Joint])));
        Joint += 1;
    }

    Frame.EndIndex = Event->JointCount;
    if (Frame.bPos)
        IkCopy3(Frame.TargetPos, (const float*)Event->TargetA);
    if (Frame.bOri)
        IkCopy9(Frame.TargetMat, (const float*)Event->TargetB);
    bool bNeed = false;
    if (Frame.bPos)
    {
        const float* const End = G_IkCurPos[Frame.EndIndex];
        const double Dx = (double)Frame.TargetPos[0] - (double)End[0];
        const double Dy = (double)Frame.TargetPos[1] - (double)End[1];
        const double Dz = (double)Frame.TargetPos[2] - (double)End[2];
        bNeed = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) > 0.001;
    }
    IkMathMatrixToQuat(Frame.TargetQ, Frame.TargetMat);
    if (!bNeed)
    {
        if (!Frame.bOri)
            return;
        if (!IkDlsOriOff(&Frame))
            return;
    }

    IkDlsSetup(&Frame);
    Frame.JointIters = Frame.bOri ? Event->JointCount : Event->JointCount - 1;
    do
    {
        float* const Cells = IkDlsCells(&Frame.Block);
        int Limit = Event->JointCount;
        if (Frame.bOri)
            Limit += 1;
        int Col = 0;
        int Link = 1;
        while (Link < Limit)
        {
            if (Frame.bPos)
            {
                float Arm[3];
                if (Link == Event->JointCount)
                {
                    Arm[0] = 0.0f;
                    Arm[1] = 0.0f;
                    Arm[2] = 0.0f;
                }
                else
                {
                    const float* Bone = (const float*)(
                        G_Engine->GetInstanceRelative(IkJointInstance(Event->JointIds[Link])));
                    IkMathTransform(Arm, G_IkCurRot[Link], Bone + 1);
                }
                if (Event->LockBytes[Link - 1] != 0)
                    IkDlsAddSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
                else
                    IkDlsWriteSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
            }
            if (Frame.bOri && Event->LockBytes[Link - 1] == 0)
                IkDlsWriteIdentity(Cells, Col + 12 * Frame.PosDim);
            if (Event->LockBytes[Link - 1] == 0)
                Col += 3;
            Link += 1;
        }
    } while (IkDlsRound(&Frame, Event));
    IkDlsWriteQuats(Event, Frame.JointIters);
}

// FUN_065fce60 (thiscall on the event, RET 0; LAB_065fb540 calls it for a chain of two or more
static void IkSolveDlsAlt(FIkEvent* Event)
{
    FIkDlsFrame Frame;
    const unsigned long Flags = Event->Flags28;
    Frame.bPos = ((Flags >> 5) & 1u) != 0;
    Frame.bOri = ((Flags >> 6) & 1u) != 0;
    Frame.Dof = Event->JointCount * 3;
    Frame.PosDim = Frame.bPos ? 3 : 0;
    Frame.TaskDim = Frame.PosDim + (Frame.bOri ? 3 : 0);
    if (Frame.bOri)
        Frame.Dof += Event->Extra30 * -3;
    else if (Frame.bPos)
        Frame.Dof += Event->Extra30 * -3;

    IkDlsReadStart(Event);
    int Joint = 0;
    while (Joint < Event->JointCount)
    {
        G_Engine->GetJointParams(IkJointInstance(Event->JointIds[Joint]), 0, Event->Quats + Joint * 4);
        IkCopy3(G_IkCurPos[Joint + 1],
                G_Engine->GetInstancePosition(IkJointInstance(Event->JointIds[Joint])));
        IkCopy9(G_IkCurRot[Joint + 1],
                G_Engine->GetInstanceOrientation(IkJointInstance(Event->JointIds[Joint])));
        Joint += 1;
    }

    Frame.EndIndex = Event->JointCount;
    if (Frame.bPos)
        IkCopy3(Frame.TargetPos, (const float*)Event->TargetA);
    if (Frame.bOri)
        IkCopy9(Frame.TargetMat, (const float*)Event->TargetB);
    bool bNeed = false;
    if (Frame.bPos)
    {
        const float* const End = G_IkCurPos[Frame.EndIndex];
        const float Fx = (float)((double)Frame.TargetPos[0] - (double)End[0]);
        const float Fy = (float)((double)Frame.TargetPos[1] - (double)End[1]);
        const float Fz = (float)((double)Frame.TargetPos[2] - (double)End[2]);
        const double X = (double)Fx;
        const double Y = (double)Fy;
        const double Z = (double)Fz;
        bNeed = std::sqrt((Y * Y + Z * Z) + X * X) > 0.001;
    }
    IkMathMatrixToQuat(Frame.TargetQ, Frame.TargetMat);
    if (!bNeed)
    {
        if (!Frame.bOri)
            return;
        if (!IkDlsOriOff(&Frame))
            return;
    }

    IkDlsSetup(&Frame);
    Frame.JointIters = Event->JointCount;
    do
    {
        float* const Cells = IkDlsCells(&Frame.Block);
        const int Limit = Event->JointCount + 1;
        int Col = 0;
        int Link = 1;
        while (Link < Limit)
        {
            if (Frame.bPos)
            {
                const float* Bone = (const float*)(
                    G_Engine->GetInstanceRelative(IkJointInstance(Event->JointIds[Link])));
                float Arm[3];
                IkMathTransform(Arm, G_IkCurRot[Link], Bone + 1);
                if (Event->LockBytes[Link - 1] != 0)
                    IkDlsAddSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
                else
                    IkDlsWriteSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
            }
            if (Frame.bOri && Event->LockBytes[Link - 1] == 0)
                IkDlsWriteIdentity(Cells, Col + 12 * Frame.PosDim);
            if (Event->LockBytes[Link - 1] == 0)
                Col += 3;
            Link += 1;
        }
    } while (IkDlsRound(&Frame, Event));
    IkDlsWriteQuats(Event, Frame.JointIters);
}

// The single-joint body LAB_065fb540 inlines (065fb5f3..b879; Bone = the IEngine +0xC4 result
static void IkSolveSingleJoint(FIkEvent* Event, FIkApplyGate* Gate, const float* Bone)
{
    void* const Joint = (void*)((ptrdiff_t)Gate->JointAt0C);
    const float* ParentRot = G_Engine->GetInstanceOrientation(G_Engine->GetInstanceParent(Joint));
    float Local[9];
    IkCopy9(Local, ParentRot);
    float Trans[9];
    if ((Event->Flags28 & 0x40) != 0)
    {
        Trans[0] = Local[0];
        Trans[1] = Local[3];
        Trans[2] = Local[6];
        Trans[3] = Local[1];
        Trans[4] = Local[4];
        Trans[5] = Local[7];
        Trans[6] = Local[2];
        Trans[7] = Local[5];
        Trans[8] = Local[8];
        AcquireMathEngine()->MulMatrix(*(FMatrix*)Local,
                                       *(const FMatrix*)Trans,
                                       *(const FMatrix*)Event->TargetB);
    }
    else
    {
        const float* Pos = G_Engine->GetInstancePosition(Joint);
        float P[3];
        IkCopy3(P, Pos);
        const float* Target = (const float*)Event->TargetA;
        float Diff[3];
        const float* Src = Target;
        if ((Event->Flags28 & 1) != 0)
        {
            Diff[0] = (float)((double)Target[0] - (double)P[0]);
            Diff[1] = (float)((double)Target[1] - (double)P[1]);
            Diff[2] = (float)((double)Target[2] - (double)P[2]);
            Src = Diff;
        }
        float Dir[3];
        IkCopy3(Dir, Src);
        const double X = (double)Dir[0];
        const double Y = (double)Dir[1];
        const double Z = (double)Dir[2];
        const double Inv = 1.0 / std::sqrt(Y * Y + Z * Z + X * X);
        Dir[0] = (float)(X * Inv);
        Dir[1] = (float)(Y * Inv);
        Dir[2] = (float)(Z * Inv);
        float Look[9];
        IkLookAt(Look, Dir, Local, (unsigned char)Event->Flags28);
        Trans[0] = Local[0];
        Trans[1] = Local[3];
        Trans[2] = Local[6];
        Trans[3] = Local[1];
        Trans[4] = Local[4];
        Trans[5] = Local[7];
        Trans[6] = Local[2];
        Trans[7] = Local[5];
        Trans[8] = Local[8];
        AcquireMathEngine()->MulMatrix(*(FMatrix*)Local,
                                       *(const FMatrix*)Trans,
                                       *(const FMatrix*)Look);
    }
    float Result[9];
    IkCopy9(Result, Local);
    if ((Event->Flags28 & 0x80) == 0)
    {
        float Clamp[9];
        const float* Clamped = IkEulerClamp(Clamp, Bone, Result);
        IkCopy9(Result, Clamped);
    }
    float Q[4];
    AcquireMathEngine()->MatrixToQuaternion(*(FQuaternion*)Q,
                                            *(const FMatrix*)Result); // +0x50
    IkCopy4(Event->Quats, Q);
}

// LAB_065fb540 (IK event vtable slot 0, thiscall RET 0x10: OutQuat, MatchId, Gate, Time),
static int DACOM_API IkApply(FIkEvent* Event, float* OutQuat, int MatchId, FIkApplyGate* Gate,
                             float Time)
{
    (void)Time;
    if ((Event->Flags34 & 1) != 0)
    {
        for (int Index = 0; Index < Event->JointCount; ++Index)
        {
            if ((int)Event->Channels[Index] == MatchId)
            {
                G_Engine->GetJointParams(
                    (void*)((ptrdiff_t)Event->JointIds[Index]), 0,
                    OutQuat);
                return 1;
            }
        }
        return 1;
    }

    const int OldTick = Event->Tick3C;
    Event->Tick3C = OldTick + 1;
    if (OldTick == 0 && Gate->SkipFlag == 0)
    {
        const float* Bone = (const float*)(G_Engine->GetInstanceRelative(
            (void*)((ptrdiff_t)Gate->JointAt0C)));
        if (Event->JointCount == 1)
            IkSolveSingleJoint(Event, Gate, Bone);
        else if ((Event->Flags34 & 2) != 0)
            IkSolveDlsAlt(Event);
        else
            IkSolveDls(Event);
    }

    if (Event->Tick3C == Event->JointCount)
        Event->Tick3C = 0;

    for (int Index = 0; Index < Event->JointCount; ++Index)
    {
        if ((int)Event->Channels[Index] == MatchId)
        {
            IkCopy4(OutQuat, Event->Quats + Index * 4);
            return 0;
        }
    }
    return 0;
}

// FUN_065f5400, thiscall (Instance) RET 0x8 (Desc, PlayExtra), 065f5400..065f5943; called
static FIkListNode* AllocAimEvent(FDeformInstance* Instance, const unsigned char* Desc,
                                  void* PlayExtra)
{
    int BindB = -1;
    if (Instance->PartArray != nullptr)
        BindB = (int)((ptrdiff_t)(Instance->PartArray[0].Lod->BoneSlots[0].Instance));

    std::vector<int> Chain;
    unsigned char bSphereAxis = 0;
    int Cursor = *(const int*)(Desc + 8);
    if (Cursor != *(const int*)(Desc + 4))
    {
        int Parent = 0;
        do
        {
            if (G_Animation != nullptr)
            {
                unsigned char bBlocked = 0;
                G_Animation->HasHandle((unsigned long)Cursor, &bBlocked);
                if (bBlocked != 0)
                {
                    int WalkBack = *(const int*)(Desc + 8);
                    while (WalkBack != Cursor)
                    {
                        G_Animation->HasAnyHandle((unsigned long)WalkBack);
                        WalkBack = (int)((ptrdiff_t)(
                            G_Engine->GetInstanceParent((void*)((ptrdiff_t)WalkBack))));
                    }
                    FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x44D,
                          G_StartAimBlocked);
                    return (FIkListNode*)((ptrdiff_t)-1);
                }
                G_Animation->RegisterHandle((unsigned long)Cursor);
            }
            Chain.push_back(Cursor);
            void* const CursorInstance = (void*)((ptrdiff_t)Cursor);
            Parent = (int)((ptrdiff_t)(G_Engine->GetInstanceParent(CursorInstance)));
            const unsigned long* Relative = G_Engine->GetInstanceRelative(CursorInstance);
            if (Relative[0] == 4)
            {
                const float* Axis = (const float*)(Relative + 4);
                bSphereAxis = 1;
                const double X = (double)Axis[0];
                const double Y = (double)Axis[1];
                const double Z = (double)Axis[2];
                const double Length = std::sqrt(X * X + Y * Y + Z * Z);
                if (!(Length > 1.0e-5))
                    bSphereAxis = 0;
            }
            Cursor = Parent;
        } while (Parent != *(const int*)(Desc + 4));
    }
    const int Count = (int)(Chain.size());
    std::vector<int> JointIds(Chain.size());
    for (int Index = 0; Index < Count; ++Index)
        JointIds[Index] = Chain[Count - 1 - Index];

    FIkListNode* Node = new FIkListNode;
    std::memset(Node, 0, sizeof(FIkListNode));
    Node->Active = 0;
    Node->Event = (FIkEvent*)((ptrdiff_t)-1);
    LinkListNode(Instance, Node);
    Node->Active = 1;

    // 065f55c6..0601: operator new 0x44 (not cleared), then the inlined constructor -- +0x24,
    // +0x34 &= ~1, +0x20, the table 06601340 (installed by `new FIkEvent`), +0x3C = 0.
    FIkEvent* Event = new FIkEvent;
    Event->TargetB = *(void* const*)(Desc + 0x14);
    Event->Flags34 = (unsigned char)(Event->Flags34 & 0xFE);
    Event->TargetA = *(void* const*)(Desc + 0x10);
    Event->Tick3C = 0;
    Node->Event = Event;
    Event->Owner = Node;

    const size_t Count4 = (size_t)Count * 4;
    Event->JointIds = (int*)new unsigned char[Count4];
    if (Count > 0)
        std::memcpy(Event->JointIds, &JointIds[0], Count4);
    Event->Channels = (void**)new unsigned char[Count4];
    Event->Quats = (float*)new unsigned char[(size_t)Count * 0x10];
    Event->Flags28 = *(const unsigned long*)(Desc + 0x0C);
    Event->StartJoint = *(const int*)(Desc + 4);
    Event->EndJoint = *(const int*)(Desc + 8);
    Event->BindMats = (float*)new unsigned char[(size_t)Count * 0x24];
    Event->LockBytes = new unsigned char[(size_t)Count];
    std::memset(Event->LockBytes, 0, (size_t)Count);
    Event->Damp38 = *(const float*)(Desc + 0x18);
    Event->Extra30 = *(const int*)(Desc + 0x1C);

    if (!SetIkJointCount(Event, (unsigned int)Count))
    {
        StopAimNode(Node);
        return (FIkListNode*)((ptrdiff_t)-1);
    }

    unsigned long PlayBlob[7];
    PlayBlob[0] = 0;
    PlayBlob[1] = (*(const unsigned long*)(Desc + 0x0C) >> 1) & 0x80u;
    const float One = 1.0f;
    std::memcpy(&PlayBlob[2], &One, sizeof(One));
    PlayBlob[3] = (unsigned long)((ptrdiff_t)PlayExtra);
    std::memcpy(&PlayBlob[4], &One, sizeof(One));
    std::memcpy(&PlayBlob[5], &One, sizeof(One));
    PlayBlob[6] = 0;
    unsigned long CreateCookie[4];
    CreateCookie[0] = 0;
    CreateCookie[1] = 0;
    CreateCookie[2] = 0;
    CreateCookie[3] = (unsigned long)-1;
    for (int Index = 0; Index < Count; ++Index)
    {
        const int JointId = JointIds[Index];
        void* const JointInstance = (void*)((ptrdiff_t)JointId);
        CreateCookie[3] = (unsigned long)JointId;
        const unsigned long Key = G_Channel->CreateTyped(
            (IChannelOwner*)Event, 4, CreateCookie, (void*)((ptrdiff_t)BindB));
        Event->Channels[Index] = (void*)((ptrdiff_t)Key);
        G_Channel->Play(Key, (float*)PlayBlob);

        const float* Relative = (const float*)(G_Engine->GetInstanceRelative(JointInstance));
        // 065f57ad..57e1: FLD hi, FADD lo, FMUL double 0.5 (DAT_06601338), FSTP float.
        const float Yaw = (float)(
            ((double)Relative[0x50 / 4] + (double)Relative[0x4C / 4]) * 0.5);
        const float Pitch = (float)(
            ((double)Relative[0x58 / 4] + (double)Relative[0x54 / 4]) * 0.5);
        const float Roll = (float)(
            ((double)Relative[0x44 / 4] + (double)Relative[0x40 / 4]) * 0.5);
        // 065f57e5..587c: written into BindMats[i], then overwritten whole at 065f58d8 before
        // anything reads it -- the listing does both, and so does the port.
        IkEulerRebuild(Event->BindMats + Index * 9, Yaw, Pitch, Roll);

        // Retail's target is [E+0x78] with the matrix at [E+0x88]: a loose joint (seven
        // floats from EngBase's GetJointParams) runs into the matrix slot before
        // QuaternionToMatrix rewrites it, so the buffer here holds seven.
        float JointQuat[7];
        G_Engine->GetJointParams(JointInstance, 0, JointQuat);
        float LiveMat[9];
        IkMathQuatToMatrix(LiveMat, JointQuat);
        std::memcpy(Event->BindMats + Index * 9, LiveMat, sizeof(LiveMat));

        const int LockCount = *(const int*)(Desc + 0x1C);
        const int* LockIds = (const int*)(Desc + 0x20);
        for (int Lock = 0; Lock < LockCount; ++Lock)
        {
            if (LockIds[Lock] == JointId)
            {
                Event->LockBytes[Index] = 1;
                break;
            }
        }
    }
    Event->Flags34 = (unsigned char)((((bSphereAxis << 1) ^ Event->Flags34) & 2) ^
                                                Event->Flags34);
    return Node;
}

// FUN_065f14f0: parent-link walk from BindCursor. Not CCD / svdcmp.
static bool LinkBoneByParent(FLodHolder* Holder, void* BoneInst, int Parent)
{
    FSkeleton* Skel = Holder->Skeleton;
    int Cursor = Holder->BindCursor;
    if (Cursor >= Skel->BoneCount)
        return false;

    while (Skel->Bones[Cursor].Parent != Parent)
    {
        Cursor += 1;
        if (Cursor >= Skel->BoneCount)
            return false;
    }

    Holder->BindCursor = Cursor;
    FBoneRuntime* Slot = BindOneBoneRuntime(Holder, Cursor, BoneInst);
    Holder->BindCursor = Holder->BindCursor + 1;
    return Slot != nullptr; // 065f153a SETNZ on FUN_065f14a0's result
}

// LAB_065f1450 cdecl: thiscall FUN_065f14f0(this=arg3, bone=arg1, parent=arg2).
static bool __cdecl BoneLinkCallbackThunk(void* BoneInst, int Parent, FLodHolder* Holder)
{
    return LinkBoneByParent(Holder, BoneInst, Parent);
}

// FUN_065f1d90 cdecl: DAT_06603150=param_1, DAT_06603154=param_2.
static bool (__cdecl* G_BoneLinkFn)(void*, int, FLodHolder*) = nullptr;
static FLodHolder* G_BoneLinkHolder = nullptr;

static void RegisterBoneLinkCallback(bool (__cdecl* Fn)(void*, int, FLodHolder*), FLodHolder* Holder)
{
    G_BoneLinkFn = Fn;
    G_BoneLinkHolder = Holder;
}

// FUN_065f1550 (thiscall on the holder, RET 4: Skel), 065f1550..1657, AL = 1 always.
unsigned char FLodHolder::AttachSkeleton(FSkeleton* Skel) // formerly AttachLodSkeleton
{
    if (Skeleton != Skel)
    {
        if (Skeleton != nullptr)
        {
            ReleaseSkeleton(Skeleton);
            Skeleton = nullptr;
        }
        Skeleton = Skel;
        if (Skel != nullptr)
            AddRefSkeleton(Skel);
    }

    Skel = Skeleton;
    const int Count = Skel->BoneCount;
    FBoneRuntime* Slots =
        (FBoneRuntime*)new unsigned char[(size_t)Count * sizeof(FBoneRuntime)];
    int Index = 0;
    while (Index < Count)
    {
        InitBoneRuntimeSlot(&Slots[Index]);
        Index += 1;
    }
    BoneSlots = Slots;
    RegisterBoneLinkCallback(&BoneLinkCallbackThunk, this);
    void* RootInst = G_Engine->CreateInstance(Skel->EngineHandle, nullptr);
    RegisterBoneLinkCallback(nullptr, nullptr);
    if (RootInst == G_InvalidEngineHandle)
        return 1;

    int BoneIndex = 0;
    while (BoneIndex < Skeleton->BoneCount)
    {
        if (BoneSlots[BoneIndex].Instance == G_InvalidEngineHandle)
        {
            void* ArchHandle = (void*)(
                (ptrdiff_t)(Skeleton->Bones[BoneIndex].Parent));
            void* BoneInst = G_Engine->CreateInstance(ArchHandle, nullptr);
            FBoneRuntime* Slot = BindOneBoneRuntime(this, BoneIndex, BoneInst);
            Slot->Flags = (unsigned char)(Slot->Flags | 1u);
        }
        BoneIndex += 1;
    }
    return 1;
}

// FUN_065f67f0 (thiscall on the part, RET 0xC: Holder, Meshes, Count), 065f67f0..68b9, AL = 1.
unsigned char FDeformPart::BindMeshes(FLodHolder* Holder, FDeformMesh** InMeshes, int Count) // formerly BindMeshesToPart
{
    if (Lod != Holder)
    {
        if (Lod != nullptr)
        {
            ReleaseLodHolder(Lod);
            Lod = nullptr;
        }
        Lod = Holder;
        if (Holder != nullptr)
            AddRefLodHolder(Holder);
    }

    MeshCount = Count;
    int* Block = (int*)new unsigned char[(size_t)Count * 4 + 4];
    *Block = Count;
    FDeformMesh** Array = (FDeformMesh**)(Block + 1);
    int Index = 0;
    while (Index < Count)
    {
        Array[Index] = nullptr;
        Index += 1;
    }
    Meshes = Array;

    int MaxVertexCapacity = 0;
    int MaxIndexCount = 0;
    Index = 0;
    while (Index < MeshCount)
    {
        Meshes[Index] = InMeshes[Index];
        const int VertexCapacity = Meshes[Index]->GetVertexCapacity();
        if (VertexCapacity > MaxVertexCapacity)
            MaxVertexCapacity = VertexCapacity;
        const int IndexCount = Meshes[Index]->GetIndexCount();
        if (IndexCount > MaxIndexCount)
            MaxIndexCount = IndexCount;
        Index += 1;
    }
    if (MaxVertexCapacity > 0)
        Vb.Capacity = MaxVertexCapacity;
    if (MaxIndexCount > 0)
        Ib.Capacity = MaxIndexCount;
    return 1;
}

// FUN_065fdfb0 (cdecl: the IMaterialLibrary), 065fdfb0..dfcb: FUN_065fdfd0 (result dropped),
// then +0xC (0, "^eye*", "EyeMaterial"). No test.
static void BindEyeMaterials(IMaterialLibrary* MaterialLib)
{
    RegisterEyeMaterialFactory();
    MaterialLib->AddMapRule(0, G_EyeMaterialPattern,
                                                       G_EyeMaterialName);
}

// FUN_065f1df0: Release Pipeline / MatLib / IB / VB only (listing order).
static void ReleaseRenderCaches()
{
    ReleaseInterface(G_Pipeline);
    ReleaseInterface(G_MaterialLibrary);
    ReleaseInterface(G_IndexBuffer);
    ReleaseInterface(G_VertexBuffer);
}

// FUN_065f1810 DAT_06603128..DAT_06603140 store 0, no Release.
static void ZeroModuleInterfaceCaches()
{
    G_System = nullptr;
    G_MaterialLibrary = nullptr;
    G_Pipeline = nullptr;
    G_Engine = nullptr;
    G_Animation = nullptr;
    G_Channel = nullptr;
    G_Renderer = nullptr;
}

// FUN_065f18d0: 1df0 + Release Extra14. Anim/Engine/Channel/Renderer are non-owning after QI+Release.
static void ReleaseComponentCaches()
{
    ReleaseRenderCaches();
    ReleaseInterface(G_System);
}

// Reused parser buffer; a failed read leaves its contents unchanged.
static char G_DeformRateValue[0x400];

// FUN_065f1900's value parse, 065f19cd..1b11: the 36-character charset (.data:06602138) is
static unsigned long ParseDeformRateValue(char* Text)
{
    const size_t Span = strspn(Text, G_RateCharset);
    if (Span != 0)
        Text[Span] = '\0';

    if (_stricmp(Text, "true") == 0) // DACOM's stricmp is _stricmp (DacomStricmp)
        return 1;
    if (_stricmp(Text, "yes") == 0)
        return 1;
    if (_stricmp(Text, "false") == 0)
        return 0;
    if (_stricmp(Text, "no") == 0)
        return 0;

    if (strchr(Text, 'x') != nullptr || strchr(Text, 'X') != nullptr)
        return strtoul(Text, nullptr, 16);

    strspn(Text, "0123456789+-");
    return (unsigned long)(MsvcrtAtoi(Text));
}

// Widen the unsigned rate before dividing, then round the result to float.
static void ApplyParsedRate(unsigned long Rate)
{
    if (Rate == 0)
        G_DeformRate = 0.0f;
    else
        G_DeformRate = (float)(1.0 / (double)Rate);
}

// FUN_065f1900 (thiscall on the component, RET 4: Desc), 065f1900..1be5.
static GENRESULT ApplyCreateInstanceInit(FDeformableObject* Object, FAggDesc18* Desc)
{
    G_System = Desc->SystemAt14;
    if (G_System == nullptr)
        return GR_GENERIC;

    G_System->AddRef();
    ICOManager* Dacom = DACOM_Acquire();
    IProfileParser* First = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)(&First)) >= 0)
    {
        IProfileParser* Parser = nullptr; // the holder at entry+4
        if (First != nullptr)
        {
            Parser = First;
            Parser->AddRef();
        }
        unsigned long Rate = 0;
        bool bParse = true;
        if (Parser == nullptr &&
            Dacom->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) != 0)
        {
            Rate = (unsigned long)((ptrdiff_t)Parser); // 065f1b23
            bParse = false;
        }
        if (bParse)
        {
            void* const Section = Parser->OpenSection(G_DeformableSection, 3);
            if (Section != nullptr)
            {
                if (Parser->GetString(Section, G_DeformRateKey, G_DeformRateValue, 0x400) != 0)
                    Rate = ParseDeformRateValue(G_DeformRateValue);
                Parser->CloseSection(Section);
            }
        }
        if (Parser != nullptr) // FUN_065f9170
        {
            Parser->Release();
            Parser = nullptr;
        }
        ApplyParsedRate(Rate);
    }

    if (Object->QueryInterface(Iids::Engine, (void**)(&G_Engine)) < 0)
    {
        LogDeformError(0xB5, "Deform: Initialize: Unable to acquire IID_IEngine, unable to continue.");
        ReleaseInterface(G_System);
        if (First != nullptr)
            First->Release();
        return GR_GENERIC;
    }
    G_Engine->Release();
    if (First != nullptr)
        First->Release();
    return GR_OK;
}

// One QI of LAB_065f1bf0: straight into the module cache (no pre-clear, no test of Source or
// of the answer pointer); a negative result (the JGE not taken) -> FDUMP ERROR at Line,
// FUN_065f1df0, false.
static bool QueryRequired(IDAComponent* Source, const char* Iid, void** Out, int Line,
                          const char* Error)
{
    if (Source->QueryInterface(Iid, Out) >= 0)
        return true;
    LogDeformError(Line, Error);
    ReleaseRenderCaches();
    return false;
}

// FUN_065f6720
// FUN_065f6720 (thiscall): dwords +0..+0x30 = 0, then byte +0x30 |= 1. No test.
static void ConstructPart(FDeformPart* Part)
{
    std::memset(Part, 0, sizeof(FDeformPart));
    Part->Flags = 1;
}

static GENRESULT DACOM_API StubRet8(void* /*This*/, void* /*A*/);

// IChannelSink slot 0 (EngBase FUN_0661b420 OnKeys).
void DACOM_API FDeformInstance::OnKeys(unsigned long ChannelKey, void* Context,
                                       FChannelKeyBatch* Batch)
{
    // FUN_065f5210 (RET 0x10), 065f5210..5281: +0x18 null -> return (EAX = that 0). Cookie (the
    if (UserAt18 == nullptr)
        return;
    unsigned long Cookie = 0;
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Handle =
                (unsigned long)((ptrdiff_t)Node->Event);
            if (G_Animation->HasObject(Handle, AsOpaqueDword(ChannelKey)) != 0)
            {
                G_Animation->GetFirstInstanceField(Handle, &Cookie);
                break;
            }
        }
        Node = Node->Next;
    }
    UserAt18->OnKeys(Cookie, Context, Batch);
}

// IChannelSink slot 1. LAB_065f5290 (RET 0xC), 065f5290..52a4: +0x18 null -> return (EAX = that
// 0); else replace this with +0x18 and JMP its slot 1.
void DACOM_API FDeformInstance::OnOpaqueNotification(void* A, void* B) // formerly Slot01
{
    if (UserAt18 == nullptr)
        return;
    UserAt18->OnOpaqueNotification(A, B);
}

// IChannelSink slot 2 (EngBase FUN_0661b420 OnLoop): match anim, +0x60 duration, half-angle
// quat, Math+0x54, UserAt18 slot 2.
void DACOM_API FDeformInstance::OnLoop(unsigned long ChannelKey, float* Transform, void* Context)
{
    // LAB_065f52b0 (RET 0x10), 065f52b0..53f5. First node with Active == 0 and IAnimation2
    float* const Xform = Transform;
    int Handle = -1;
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Stored =
                (unsigned long)((ptrdiff_t)Node->Event);
            if (G_Animation->HasObject(Stored, AsOpaqueDword(ChannelKey)) != 0)
            {
                Handle = (int)Stored;
                break;
            }
        }
        Node = Node->Next;
    }
    if (Node == nullptr)
        return;

    float Duration = 0.0f;
    float Height = 0.0f;
    if (G_Animation->GetDurationIfNamed((unsigned long)Handle, &Duration) != 0)
        Height = Duration + Offset20;
    else
        Height = Offset24 + Offset20;
    Xform[0x28 / 4] = Height;
    const double Half =
        std::atan2((double)Xform[0x8 / 4], (double)Xform[0x20 / 4]) *
        0.5;
    const float SinScaled = (float)(std::sin(Half) / std::sqrt(1.0));
    float Quat[4];
    Quat[0] = (float)(std::cos(Half));
    const float Side = SinScaled * 0.0f;
    WriteVec3(&Quat[1], Side, SinScaled, Side);
    float Matrix[9];
    I3DMathEngine* Math = AcquireMathEngine();
    Math->QuaternionToMatrix(*(FMatrix*)Matrix,
                             *(const FQuaternion*)Quat); // +0x54
    std::memcpy(Xform, Matrix, sizeof(Matrix));
    if (UserAt18 == nullptr)
        return;
    UserAt18->OnLoop(ChannelKey, Xform, Context);
}

// FUN_065f3650
static void ConstructInstanceBody(FDeformInstance* Instance)
{
    Instance->PartCount = 0;
    Instance->PartArray = nullptr;
    Instance->ListHead = nullptr;
    Instance->ListTail = nullptr;
    Instance->ListCount = 0;
    Instance->UserAt18 = nullptr;
    Instance->UserAt1C = nullptr;
    Instance->Offset20 = 0.0f;
    Instance->Offset24 = 0.0f;
    if (G_DeformRate > 0.0f)
        Instance->RateCookie = (float)(rand()) * G_DeformRate * G_RandToUnit;
    else
        Instance->RateCookie = G_DeformRate;
}

// LAB_065f59b0 cdecl thunk: user(fn)(bone_inst, hp_info, user_data).
struct FHardpointWalkCtx
{
    void* UserFn;
    void* UserData;
    void* BoneInst;
};

// The first argument is the hardpoint's NAME, not a blob -- see FHardpointEnumProc in
static void __cdecl HardpointWalkThunk(char const* Name, void* Context)
{
    FHardpointWalkCtx* Ctx = (FHardpointWalkCtx*)Context;
    using FnUser = void(__cdecl*)(void*, char const*, void*);
    ((FnUser)Ctx->UserFn)(Ctx->BoneInst, Name, Ctx->UserData);
}

// Pair bone instances by hardpoint-name CRC for connection.
struct FHardpointInstancePair
{
    void* First;  // node +0x10
    void* Second; // node +0x14
};

struct FHardpointConnectContext
{
    std::map<unsigned long, FHardpointInstancePair> Pairs;
    IHardpoint* Hardpoint;
};

// LAB_065f3540 (cdecl: BoneInst, Name, Context), 065f3540..363b. {CRC32(Name), {-1, -1}} is
static void __cdecl HardpointConnectCallback(void* BoneInst, char const* Name, void* Context)
{
    FHardpointConnectContext* Ctx = (FHardpointConnectContext*)Context;
    FHardpointInstancePair Unset;
    Unset.First = G_InvalidEngineHandle;
    Unset.Second = G_InvalidEngineHandle;
    std::pair<std::map<unsigned long, FHardpointInstancePair>::iterator, bool> Inserted =
        Ctx->Pairs.insert(std::make_pair(DACOM_CRC::GetCRC32(Name), Unset));
    FHardpointInstancePair* Pair = &Inserted.first->second;
    if (Pair->First == G_InvalidEngineHandle)
    {
        Pair->First = BoneInst;
        return;
    }
    Pair->Second = BoneInst;
    FHardpointKey KeyA;
    KeyA.Name = Name;
    KeyA.Crc = DACOM_CRC::GetCRC32(Name);
    FHardpointKey KeyB;
    KeyB.Name = Name;
    KeyB.Crc = DACOM_CRC::GetCRC32(Name);
    if (Ctx->Hardpoint->ConnectHardpoints(Pair->First, &KeyA, Pair->Second, &KeyB) != 0)
    {
        char Message[0x2000];
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s", G_FdumpFileDeformableObject, 0x53,
              FormatDeformMessage(Message, "Deformable: create: cannot connect parts %s<->%s",
                                  Name, Name));
    }
}

// FUN_065f59d0 (thiscall on the instance, RET 8: UserFn, UserData), 065f59d0..5ada.
static unsigned char WalkInstanceHardpoints(FDeformInstance* Instance, void* UserFn,
                                            void* UserData)
{
    IHardpoint* Hardpoint = nullptr;
    if (G_Engine->QueryInterface(Iids::Hardpoint, (void**)(&Hardpoint)) < 0)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x4EF,
              "Deform: Unable to acquire IID_IHardpoint, unable to continue");
        if (Hardpoint != nullptr)
            Hardpoint->Release();
        return 0;
    }

    int PartIndex = 0;
    while (PartIndex < Instance->PartCount)
    {
        FLodHolder* Lod = Instance->PartArray[PartIndex].Lod;
        int Bone = 0;
        while (Bone < Lod->BindCursor)
        {
            FHardpointWalkCtx Ctx;
            Ctx.UserFn = UserFn;
            Ctx.UserData = UserData;
            Ctx.BoneInst = Lod->BoneSlots[Bone].Instance;
            Hardpoint->EnumerateHardpoints(
                &HardpointWalkThunk,
                (void*)((ptrdiff_t)(Lod->Skeleton->Bones[Bone].Parent)),
                &Ctx);
            Bone += 1;
        }
        PartIndex += 1;
    }
    if (Hardpoint != nullptr)
        Hardpoint->Release();
    return 1;
}

// FUN_065f36c0 (thiscall on the instance, RET 0x10: Parts, Arg2, Arg3), 065f36c0..3e61, AL = 1.
static bool ConnectInstanceParts(FDeformInstance* Instance, int* Parts, void* Arg3, void* Arg4)
{
    Instance->UserAt18 = (IChannelSink*)Arg3;
    Instance->UserAt1C = Arg4;
    Instance->PartCount = Parts[0];
    int* Block = (int*)new unsigned char[(size_t)Instance->PartCount * sizeof(FDeformPart) + 4];
    *Block = Instance->PartCount;
    FDeformPart* Array = (FDeformPart*)(Block + 1);
    int Index = 0;
    while (Index < Instance->PartCount)
    {
        ConstructPart(&Array[Index]);
        Index += 1;
    }
    Instance->PartArray = Array;

    FDeformPartDesc* Descs = *(FDeformPartDesc**)(Parts + 1);
    int PartIndex = 0;
    while (PartIndex < Instance->PartCount)
    {
        FDeformPartDesc* Entry = &Descs[PartIndex];
        const char* Path = Entry->Path;
        IComponentFactory* Factory = nullptr;
        if (Path == nullptr)
        {
            Factory = Entry->Lods[0].FileSystem;
            Path = Entry->Lods[0].Name;
        }
        FFileSystemDesc RootDesc;
        FillFileDesc(&RootDesc, Path);
        IFileSystem* RootFs = nullptr;
        GENRESULT Created = 0;
        if (Factory != nullptr)
            Created = Factory->CreateInstance((AGGDESC*)(&RootDesc),
                                              (void**)(&RootFs));
        else
            Created = DACOM_Acquire()->CreateInstance((AGGDESC*)(&RootDesc),
                                                      (void**)(&RootFs));
        if (Created < 0)
        {
            if (RootFs != nullptr)
            {
                RootFs->Release();
                RootFs = nullptr;
            }
            PartIndex += 1;
            continue;
        }

        char BaseName[0x104];
        std::strncpy(BaseName, std::strrchr(Path, '\\') + 1, 0x104);
        char* Dot = std::strchr(BaseName, '.');
        Dot[0] = '\0';
        Dot[1] = '\0';
        char SkelName[0x100];
        _snprintf(SkelName, 0x100, "skel_%s", BaseName);

        FSkeleton* Skel = ConstructSkeleton();
        if (!LoadSkeleton(Skel, SkelName, RootFs))
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0xB0,
                  "couldnt load skeleton");
        }
        else
        {
            FLodHolder* Holder = new FLodHolder;
            Holder->Skeleton = nullptr;
            Holder->BoneSlots = nullptr;
            Holder->BindCursor = 0;
            Holder->RefCount = 1;
            if (!Holder->AttachSkeleton(Skel))
            {
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0xB8,
                      "couldnt create skeleton");
            }
            else
            {
                float Fractions[8];
                int LodCap = 0;
                if (RootFs->OpenNode("MultiLevel") != 0)
                {
                    unsigned int Wanted = 8;
                    FFileSystemDesc FractionDesc;
                    FillFileDesc(&FractionDesc, "Fractions");
                    const int Handle = RootFs->OpenByDesc(&FractionDesc);
                    if (Handle != -1)
                    {
                        const unsigned int Available =
                            RootFs->GetSize((unsigned long)Handle, 0) >> 2;
                        if (Available < Wanted)
                            Wanted = Available;
                        unsigned long Got = 0;
                        RootFs->Read((unsigned long)Handle, Fractions, Wanted * 4, &Got,
                                     0);
                        LodCap = (int)Wanted;
                        RootFs->CloseFile((unsigned long)Handle);
                    }
                }
                else
                {
                    LodCap = 1;
                    Fractions[0] = 1.0f;
                }
                if (LodCap > Entry->LodCount)
                    LodCap = Entry->LodCount;

                if (LodCap <= 0)
                {
                    Entry->LodCount = 0;
                    FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject,
                          0xD7, "no meshes found");
                }
                else
                {
                    FDeformMesh* Loaded[8];
                    int LoadedCount = 0;
                    WIN32_FIND_DATAA FindData;
                    const int Finder = FsFindFirst(RootFs, "Mesh*", &FindData);
                    if (Finder != -1)
                    {
                        unsigned int LodIndex = 0;
                        for (;;)
                        {
                            bool bSkip = false;
                            if (LodIndex == 0)
                                bSkip = (Entry->Flags & 2) == 2;
                            else if ((Entry->Flags & 1) == 1)
                                break;
                            if (!bSkip)
                            {
                                FFileSystemDesc MeshDesc;
                                FillFileDesc(&MeshDesc, FindData.cFileName);
                                MeshDesc.Field30 = (unsigned long)Finder;
                                IFileSystem* MeshFs = nullptr;
                                bool bFull = false;
                                if (RootFs->CreateInstance((AGGDESC*)(&MeshDesc),
                                                           (void**)(&MeshFs)) >= 0)
                                {
                                    Dot[0] = (char)('0' + LodIndex);
                                    const unsigned char bFlag70 =
                                        (LodIndex < Entry->LodLimit) ? 1 : 0;
                                    FDeformMesh* Mesh = LoadDeformMesh(Holder, MeshFs, BaseName, bFlag70);
                                    if (Mesh != nullptr)
                                    {
                                        Loaded[LoadedCount] = Mesh;
                                        Entry->Lods[LoadedCount].Fraction = Fractions[LodIndex];
                                        LoadedCount += 1;
                                        bFull = LoadedCount >= LodCap;
                                    }
                                }
                                if (MeshFs != nullptr)
                                    MeshFs->Release();
                                if (bFull)
                                    break;
                            }
                            LodIndex += 1;
                            if (LodIndex == 0)
                                break;
                            if (FsFindNext(RootFs, Finder, &FindData) == 0)
                                break;
                        }
                        FsFindClose(RootFs, Finder);
                    }
                    Entry->LodCount = LoadedCount;
                    if (!Instance->PartArray[PartIndex].BindMeshes(Holder, Loaded, LoadedCount))
                    {
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              G_FdumpFileDeformableObject, 0x11F, "warning: couldnt create part.");
                    }
                }
            }
        }
        // 065f3961..3985 / 065f3c29..3c44: the skeleton reference taken by the ctor, then RootFs.
        ReleaseSkeleton(Skel);
        if (RootFs != nullptr)
        {
            RootFs->Release();
            RootFs = nullptr;
        }
        PartIndex += 1;
    }

    FHardpointConnectContext Context;
    Context.Hardpoint = nullptr;
    if (G_Engine->QueryInterface(Iids::Hardpoint, (void**)(&Context.Hardpoint)) < 0)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x2B,
              "Deform: Unable to acquire IID_IHardpoint, unable to continue");
    }
    WalkInstanceHardpoints(Instance, (void*)(&HardpointConnectCallback), &Context);

    PartIndex = 0;
    while (PartIndex < Instance->PartCount)
    {
        FLodHolder* Lod = Instance->PartArray[PartIndex].Lod;
        int Bone = 1;
        while (Bone < Lod->BindCursor)
        {
            FBoneRuntime* Slot = &Lod->BoneSlots[Bone];
            if (G_Engine->GetInstanceParent(Slot->Instance) == G_InvalidEngineHandle)
            {
                G_Engine->DestroyInstance(Slot->Instance);
                Slot->Instance = G_InvalidEngineHandle;
            }
            Bone += 1;
        }
        PartIndex += 1;
    }

    if (Context.Hardpoint != nullptr)
    {
        Context.Hardpoint->Release();
        Context.Hardpoint = nullptr;
    }
    return true;
}

// FUN_065f3ed0 (thiscall on the instance), 065f3ed0..3fc5.
static void TeardownInstance(FDeformInstance* Instance)
{
    FIkListNode* Node = Instance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active != 0)
        {
            if (Node->Event != nullptr)
                delete Node->Event;                              // FUN_065fbf40 + operator delete
            Node->Event = nullptr;
        }
        else
        {
            G_Animation->DestroyScript(Node->Event);
            Node->Event = (FIkEvent*)((ptrdiff_t)-1);
        }
        Node = Node->Next;
    }
    while (Instance->ListCount != 0)
    {
        FIkListNode* Head = Instance->ListHead;
        UnlinkListNode(Instance, Head);
        delete Head;
    }

    if (Instance->PartArray != nullptr)
    {
        void* Root = Instance->PartArray[0].Lod->BoneSlots[0].Instance;
        if (Root != G_InvalidEngineHandle)
            G_Engine->DestroyInstance(Root);
        int* Header = (int*)Instance->PartArray - 1;
        int Index = *Header;
        while (Index > 0)
        {
            Index -= 1;
            Instance->PartArray[Index].Reset();
        }
        delete[] (unsigned char*)Header;
        Instance->PartArray = nullptr;
    }
    Instance->PartCount = 0;
}

// --- inner IDAComponent @ +0x0C (FDeformableInner: LAB_065f2400 / 065fe060 / 065f24c0) ---

// .rdata:066012d0 (PE bytes): {IID string, offset from the owner}, null-terminated. An offset
// with bit 31 set would name a pointer member instead of an embedded facet; none here does.
struct FInnerIidEntry
{
    const char* Iid;
    int Offset;
};

static const FInnerIidEntry G_InnerIidTable[] = {
    {G_IidDeformableShort, 0},
    {G_IidEngineComponentShort, 4},
    {Iids::Deformable, 0},
    {Iids::EngineComponent, 4},
    {Iids::AggregateComponent, 4},
    {nullptr, 0},
};

// LAB_065f2400 (RET 0xC), 065f2400..24af: walk the table with an inlined strcmp; hit ->
GENRESULT DACOM_API FDeformableInner::QueryInterface(const char* Iid, void** Out)
{
    int Index = 0;
    while (G_InnerIidTable[Index].Iid != nullptr)
    {
        if (std::strcmp(G_InnerIidTable[Index].Iid, Iid) == 0)
        {
            unsigned char* OwnerBytes = (unsigned char*)Owner;
            const int Offset = G_InnerIidTable[Index].Offset;
            IDAComponent* Facet = nullptr;
            if (Offset < 0)
                Facet = *(IDAComponent**)(OwnerBytes + (Offset & 0x7fffffff));
            else
                Facet = (IDAComponent*)(OwnerBytes + Offset);
            Facet->AddRef();
            *Out = Facet;
            return GR_OK;
        }
        Index += 1;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// LAB_065fe060 (ICF-shared): ++[this+4], EAX = the new count.
unsigned long DACOM_API FDeformableInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// LAB_065f24c0 (RET 4), 065f24c0..24f8: [this+4] > 0 (unsigned, JBE) -> decrement; still
// non-zero -> return it. Zero -> [this+4] = 1; owner [this+8] non-null -> FUN_065f18d0 on it
// + operator delete; return 0.
unsigned long DACOM_API FDeformableInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FDeformableObject* Object = Owner;
    if (Object != nullptr)
    {
        delete Object;
    }
    return 0;
}

// --- IDeformable @ +0: QI/AddRef/Release delegate to [this+8] (LAB_065f2500) ---

// LAB_065f2500/2510/2520: [ESP+4] = [this+8] (Controlling), JMP [vtable+0/4/8] -- no tests.
static GENRESULT DACOM_API DeformableQueryInterface(FDeformableObject* This, const char* Iid,
                                                    void** Out)
{
    IDAComponent* Controlling = This->Controlling;
    return Controlling->QueryInterface(Iid, Out);
}

static unsigned long DACOM_API DeformableAddRef(FDeformableObject* This)
{
    return This->Controlling->AddRef();
}

static unsigned long DACOM_API DeformableRelease(FDeformableObject* This)
{
    return This->Controlling->Release();
}

// Shared fail stubs. First arg is this; RET = (1 + extra) * 4, EAX = -1.
static GENRESULT DACOM_API StubRet8(void* /*This*/, void* /*A*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRetC(void* /*This*/, void* /*A*/, void* /*B*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet10(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet14(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet20(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/, void* /*E*/, void* /*F*/, void* /*G*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet28(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/, void* /*E*/, void* /*F*/, void* /*G*/, void* /*H*/,
                                     void* /*I*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API DeformableCreate(FDeformable* /*This*/, void** OutInstance,
                                            int* Parts, void* Arg3, void* Arg4, void* /*Arg5*/)
{
    // Reject an empty parts table before constructing an instance.
    if (Parts[0] <= 0)
        return GR_INVALID_PARMS;

    // `new FDeformInstance` sets the vtable FUN_065f3650 stores first (PTR_FUN_06601308 --
    // EngBase calls it back as the playback's script); ConstructInstanceBody does the rest.
    // The raw operator new used before left the vtable pointer uninitialised.
    FDeformInstance* Instance = new FDeformInstance;
    ConstructInstanceBody(Instance);
    if (ConnectInstanceParts(Instance, Parts, Arg3, Arg4))
    {
        *OutInstance = Instance;
        return GR_OK;
    }
    TeardownInstance(Instance);
    delete Instance;
    *OutInstance = nullptr;
    return GR_GENERIC;
}

static GENRESULT DACOM_API DeformableCreateAlias(FDeformable* This, void** OutInstance,
                                                 int* Parts, void* Arg3, void* Arg4, void* Arg5)
{
    // LAB_065f1860: JMP [vtable+0x0C]
    return DeformableCreate(This, OutInstance, Parts, Arg3, Arg4, Arg5);
}

static GENRESULT DACOM_API DeformableDestroy(FDeformable* /*This*/, FDeformInstance* Instance)
{
    // FUN_065f1ed0 (RET 8): null -> -1. FUN_065f3ed0, then FUN_065f3640 -- the dtor, which
    // restores the vtable and JMPs to FUN_065f3ed0 again (a no-op on the emptied instance) --
    // then delete, 0.
    if (Instance == nullptr)
        return GR_GENERIC;
    TeardownInstance(Instance);
    TeardownInstance(Instance);
    delete Instance;
    return GR_OK;
}















static unsigned char PartBoundSphere(FDeformPart* Part, float* OutRadius, float* OutCenter);

// FUN_065f4070 (thiscall on the instance, RET 0x14: Unused, Camera, OutLod, RenderArg,
static void DrawInstanceParts(FDeformInstance* Instance, void* /*Unused*/, ICamera* Camera,
                              int* OutLod, void* RenderArg, unsigned long Flags)
{
    *OutLod = 0;
    if (Instance->PartCount <= 0)
        return;
    static_assert(sizeof(ICamera*) == sizeof(float), "the radius shares the camera's slot");
    float Radius = 0.0f;
    std::memcpy(&Radius, &Camera, sizeof(Radius));
    float Center[3];
    Center[0] = 0.0f;
    Center[1] = 0.0f;
    Center[2] = 0.0f;
    int Index = 0;
    do
    {
        FDeformPart* const Part = &Instance->PartArray[Index];
        PartBoundSphere(Part, &Radius, Center);
        const float* const View = Camera->GetViewTransform();
        float ViewCenter[3];
        AcquireMathEngine()->TransformXform(*(FVector*)ViewCenter,
                                            *(const FTransform*)View,
                                            *(const FVector*)Center); // +0x28
        float Probe[3];
        Probe[0] = ViewCenter[0];
        Probe[1] = ViewCenter[1];
        Probe[2] = ViewCenter[2];
        const int Rank = Camera->ClassifySphere(Probe, Radius);
        if (Rank > 2)
            DrawPartLive(Part, Camera, RenderArg);
        if (Index == 0)
            *OutLod = Rank;
        else if (*OutLod != Rank)
        {
            if (*OutLod < Rank)
                *OutLod = Rank;
            if (*OutLod >= 3)
                *OutLod = 3;
        }
        Index += 1;
    } while (Index < Instance->PartCount);

    if (*OutLod < 3)
        return;
    void* const Bone = (void*)((ptrdiff_t)(FirstBoneInstance(Instance)));
    // Submit the root instance and merge its renderer rank into the LOD result.
    const int Rank = G_Renderer->RenderInstance(
        Camera, Bone, 1.0f, Flags | 0x108ul, nullptr, RenderArg);
    if (*OutLod != Rank)
    {
        if (*OutLod < Rank)
            *OutLod = Rank;
        if (*OutLod >= 3)
            *OutLod = 3;
    }
}





















// FUN_065f68c0 (thiscall on the part, RET 8: OutRadius, OutCenter), 065f68c0..6926: slot 4
static unsigned char PartBoundSphere(FDeformPart* Part, float* OutRadius, float* OutCenter)
{
    const unsigned char bOk = Part->Meshes[0]->GetBoundSphere(OutRadius, OutCenter);
    if (bOk != 0)
    {
        I3DMathEngine* Math = AcquireMathEngine();
        float* BoneAtC = Part->Lod->BoneSlots[0].LocalTransform;
        float Transformed[3];
        Math->TransformXform(*(FVector*)Transformed,
                             *(const FTransform*)BoneAtC,
                             *(const FVector*)OutCenter); // +0x28
        OutCenter[0] = Transformed[0];
        OutCenter[1] = Transformed[1];
        OutCenter[2] = Transformed[2];
    }
    return bOk;
}



// --- IEngineComponent helpers (compiler builds IEC vtable + this-4 thunks) ---

// LAB_065f1bf0 (IEngineComponent slot 3, RET 4), 065f1bf0..1d81. FUN_065f1df0 first. Then
static GENRESULT DACOM_API EngineInitialize(void* /*EngineThis*/)
{
    ReleaseRenderCaches();

    if (!QueryRequired(G_System, Iids::RenderPipeline8B, (void**)(&G_Pipeline), 0xCC,
                       "Deform: Initialize: Unable to acquire IID_IRenderPipeline, unable to continue."))
        return GR_GENERIC;
    if (!QueryRequired(G_System, Iids::MaterialLibrary, (void**)(&G_MaterialLibrary),
                       0xD2,
                       "Deform: Initialize: Unable to acquire IID_IMaterialLibrary, unable to continue."))
        return GR_GENERIC;
    if (!QueryRequired(G_System, Iids::RPVertexBuffer, (void**)(&G_VertexBuffer),
                       0xD8,
                       "Deform: Initialize: Unable to acquire IID_IVertexBufferManager, unable to continue."))
        return GR_GENERIC;
    if (!QueryRequired(G_System, Iids::RPIndexBuffer, (void**)(&G_IndexBuffer), 0xDE,
                       "Deform: Initialize: Unable to acquire IID_IIndexBufferManager, unable to continue."))
        return GR_GENERIC;

    if (!QueryRequired(G_Engine, Iids::Animation2, (void**)(&G_Animation), 0xE6,
                       "Deform: Initialize: Unable to acquire IID_IAnimation2, unable to continue"))
        return GR_GENERIC;
    G_Animation->Release();
    if (!QueryRequired(G_Engine, Iids::Channel, (void**)(&G_Channel), 0xED,
                       "Deform: Initialize: Unable to acquire IID_IChannel, unable to continue"))
        return GR_GENERIC;
    G_Channel->Release();
    if (!QueryRequired(G_Engine, Iids::Renderer, (void**)(&G_Renderer), 0xF4,
                       "Deform: Initialize: Unable to acquire IID_IRenderer, unable to continue"))
        return GR_GENERIC;
    G_Renderer->Release();

    BindEyeMaterials(G_MaterialLibrary);
    return GR_OK;
}

static GENRESULT DACOM_API EngineUpdateOk(void* /*This*/, void* /*A*/, void* /*B*/)
{
    return GR_OK;
}

static GENRESULT DACOM_API EngineStub10(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/)
{
    return GR_GENERIC;
}

// LAB_065f1dc0 RET 0xC. cdecl DAT_06603150(arg1, arg2, DAT_06603154); EAX=0 if fn null.
static GENRESULT DACOM_API EngineCallbackTrampoline(void* /*This*/, void* Arg1, void* Arg2)
{
    if (G_BoneLinkFn == nullptr)
        return GR_OK;
    return (GENRESULT)(
        G_BoneLinkFn(Arg1, (int)((ptrdiff_t)Arg2), G_BoneLinkHolder));
}


GENRESULT DACOM_API FDeformableObject::QueryInterface(const char* Iid, void** Out)
{
    return DeformableQueryInterface(this, Iid, Out);
}

unsigned long DACOM_API FDeformableObject::AddRef()
{
    return DeformableAddRef(this);
}

unsigned long DACOM_API FDeformableObject::Release()
{
    return DeformableRelease(this);
}

// FUN_065f1810 (thiscall, RET 0), 065f1810..1852: +4 = 066011ac (IEngineComponent's own
// table), +0 = 06601110, +4 = 066010dc (the compiler installs its three tables), then
// DAT_06603128..DAT_06603140 = 0 with no Release.
FDeformable::FDeformable()
{
    ZeroModuleInterfaceCaches();
}

// FUN_065f18d0 (thiscall, RET 0), 065f18d0..18fb: +0 = 06601110, +4 = 066010dc, FUN_065f1df0,
// DAT_06603128 non-null -> Release, = 0. Called by FUN_065f22f0's failure path and the inner
// Release (065f23a7 / 065f24e7), always right before operator delete.
FDeformable::~FDeformable()
{
    ReleaseComponentCaches();
}

GENRESULT DACOM_API FDeformable::Create(void** OutInstance, int* Parts, void* Arg3, void* Arg4,
                                               void* Arg5)
{
    return DeformableCreate(this, OutInstance, Parts, Arg3, Arg4, Arg5);
}

GENRESULT DACOM_API FDeformable::CreateUnused(void* A, void* B, void* C, void* D, void* E,
                                                      void* F, void* G)
{
    return StubRet20(this, A, B, C, D, E, F, G);
}

GENRESULT DACOM_API FDeformable::CreateAlias(void** OutInstance, int* Parts, void* Arg3,
                                                   void* Arg4, void* Arg5)
{
    return DeformableCreateAlias(this, OutInstance, Parts, Arg3, Arg4, Arg5);
}

GENRESULT DACOM_API FDeformable::Destroy(void* Instance)
{
    return DeformableDestroy(this, (FDeformInstance*)Instance);
}

GENRESULT DACOM_API FDeformable::Play(void* Instance, int* Out, void* Name, void* Extra,
                                               void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                                               void* ArgG, void* ArgH, void* Script) // formerly DeformableSlot7
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1f00 (RET 0x30), 065f1f00..1f4d: null DeformInstance -> -1 without touching *Out;
    // else FUN_065f41d0, *Out = result unchecked, -1 -> -1 else 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    const int Handle =
        PlayInstanceAnim(DeformInstance, Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, Script);
    *Out = Handle;
    if (Handle == -1)
        return GR_GENERIC;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::PlayAlt(void* Instance, int* Out, void* Name, void* Extra,
                                                void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                                                void* ArgG, void* ArgH, void* Script) // formerly DeformableSlot8
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1f50 (RET 0x30), 065f1f50..1f9d: null DeformInstance -> -1 without touching *Out;
    // else FUN_065f4660, *Out = result unchecked, -1 -> -1 else 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    const int Handle =
        PlayInstanceAnimChain(DeformInstance, Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, Script);
    *Out = Handle;
    if (Handle == -1)
        return GR_GENERIC;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::DestroyAll(void* Instance) // formerly DeformableSlot9
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1fa0 → FUN_065f4c80.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DestroyAllList(DeformInstance);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::DestroyHandle(void* Instance, int Handle) // formerly DeformableSlot10
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1fc0 → FUN_065f4d30.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DestroyListByHandle(DeformInstance, Handle);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::StartAim(void* Instance, void** Out, unsigned char* AimDesc,
                                                 void* Arg3) // formerly DeformableSlot11
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1fe0 RET 0x14 -> FUN_065f5400 start_aim. Only DeformInstance is tested (065f1fe4); a
    if (DeformInstance == nullptr)
        return GR_GENERIC;

    FIkListNode* Node = AllocAimEvent(DeformInstance, AimDesc, Arg3);
    *Out = Node;
    if (Node == (FIkListNode*)((ptrdiff_t)-1))
        return GR_GENERIC;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::StopAim(void* Instance, void* Node) // formerly DeformableSlot12
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    FIkListNode* AimNode = (FIkListNode*)Node;
    // LAB_065f2010 → FUN_065f5950 stop_aim (node, not Event).
    if (DeformInstance == nullptr || AimNode == nullptr)
        return GR_GENERIC;
    StopAimNode(AimNode);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Unused13(void* A)
{
    return StubRet8(this, A);
}

GENRESULT DACOM_API FDeformable::Unused14(void* A)
{
    return StubRet8(this, A);
}

GENRESULT DACOM_API FDeformable::SetLod(void* Instance, float Dt, int* LodIds) // formerly DeformableSlot15
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2040 (RET 0x10): null DeformInstance -> -1; else FUN_065f3fd0(LodIds, Dt), 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    unsigned char bChanged = 0;
    int Index = 0;
    while (Index < DeformInstance->PartCount)
    {
        bChanged = (unsigned char)(
            bChanged | SelectPartLod(&DeformInstance->PartArray[Index], LodIds[Index]));
        Index += 1;
    }
    if (bChanged != 0)
        DeformInstance->RateCookie = -1.0f;
    const double Remaining = (double)DeformInstance->RateCookie - (double)Dt;
    DeformInstance->RateCookie = (float)Remaining;
    if (!(Remaining < 0.0) && Remaining != 0.0)
        return GR_OK;
    DeformInstance->RateCookie = G_DeformRate;
    int Part = 0;
    while (Part < DeformInstance->PartCount)
    {
        MarkPartSkinDirty(&DeformInstance->PartArray[Part]);
        Part += 1;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Draw(void* Instance, void* Camera, void* UnusedB,
                                              void* RenderArg, int* OutLod, unsigned int Flags) // formerly DeformableSlot16
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2070 (RET 0x1C) -> FUN_065f4070(inst, B, A, D, C, E), then 0. A null DeformInstance
    // stores 0 to *OutLod (065f207c, no test) and returns -1.
    if (DeformInstance == nullptr)
    {
        *OutLod = 0;
        return GR_GENERIC;
    }
    DrawInstanceParts(DeformInstance, UnusedB, (ICamera*)Camera, OutLod, RenderArg, Flags);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Unused17(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
}

GENRESULT DACOM_API FDeformable::GetFirstBone(void* Instance, int* Out) // formerly DeformableSlot18
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f20b0 (RET 0xC), 065f20b0..20ed: null DeformInstance -> *Out = -1, return -1;
    // PartArray (+8) null -> *Out = -1, return 0; else *Out = PartArray[0].Lod->BoneSlots[0]
    // .Instance ([[[+8]]+4]+4, no further tests), return 0. *Out is never tested.
    if (DeformInstance == nullptr)
    {
        *Out = -1;
        return GR_GENERIC;
    }
    if (DeformInstance->PartArray == nullptr)
    {
        *Out = -1;
        return GR_OK;
    }
    *Out = (int)(
        (ptrdiff_t)(DeformInstance->PartArray[0].Lod->BoneSlots[0].Instance));
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::SetPosePos(void* Instance, float* Pos3) // formerly DeformableSlot19
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f20f0 (RET 0xC): null DeformInstance -> -1; else FUN_065f4f00(Pos3), 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    SweepExpiredList(DeformInstance);
    float Vec[3];
    Vec[0] = Pos3[0];
    Vec[1] = Pos3[1];
    Vec[2] = Pos3[2];
    unsigned char bHit = 0;
    FIkListNode* Node = DeformInstance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Handle =
                (unsigned long)((ptrdiff_t)Node->Event);
            float Duration;
            float MappedA[3];
            float MappedB[3];
            MappedA[0] = 0.0f;
            MappedA[1] = 0.0f;
            MappedA[2] = 0.0f;
            MappedB[0] = 0.0f;
            MappedB[1] = 0.0f;
            MappedB[2] = 0.0f;
            double Sum = 0.0;
            if (G_Animation->GetDurationIfNamed(Handle, &Duration) != 0)
            {
                G_Animation->TouchMapped(Handle, MappedB, MappedA);
                Sum = (double)Duration + (double)MappedA[1];
            }
            else
                Sum = (double)DeformInstance->Offset24 + 0.0;
            const float Y = (float)(Sum + (double)DeformInstance->Offset20);
            Vec[1] = Y;
            float OverlayPos[3];
            OverlayPos[0] = Pos3[0];
            OverlayPos[1] = Y;
            OverlayPos[2] = Pos3[2];
            G_Animation->SetOverlayPos(Handle, OverlayPos);
            bHit = 1;
        }
        Node = Node->Next;
    }
    if (bHit == 0)
        Vec[1] = DeformInstance->Offset24 + DeformInstance->Offset20;
    G_Engine->SetInstancePosition((void*)((ptrdiff_t)(
                                      FirstBoneInstance(DeformInstance))),
                                  Vec);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::SetPoseOri(void* Instance, void* Ori) // formerly DeformableSlot20
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2110 (RET 0xC): null DeformInstance -> -1; else FUN_065f5050(Ori), 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    FIkListNode* Node = DeformInstance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
            G_Animation->SetOverlayExtra(
                (unsigned long)((ptrdiff_t)Node->Event),
                (float*)Ori);
        Node = Node->Next;
    }
    G_Engine->SetInstanceOrientation(
        (void*)((ptrdiff_t)(FirstBoneInstance(DeformInstance))), Ori);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Unused21(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused22(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused23(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::SetHeight(void* Instance, float Height) // formerly DeformableSlot24
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2150 (RET 0xC): null DeformInstance -> -1; else FUN_065f50b0(Height), 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DeformInstance->Offset20 = Height;
    SweepExpiredList(DeformInstance);
    FIkListNode* Node = DeformInstance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Handle =
                (unsigned long)((ptrdiff_t)Node->Event);
            float Duration = 0.0f;
            float Base = 0.0f;
            float Add = 0.0f;
            float Ref = 0.0f;
            if (G_Animation->GetDurationIfNamed(Handle, &Duration) != 0)
            {
                float MappedA[3];
                float MappedB[3];
                MappedA[0] = 0.0f;
                MappedA[1] = 0.0f;
                MappedA[2] = 0.0f;
                MappedB[0] = 0.0f;
                MappedB[1] = 0.0f;
                MappedB[2] = 0.0f;
                G_Animation->TouchMapped(Handle, MappedA, MappedB);
                Base = Duration;
                Add = MappedB[1];
                Ref = MappedA[1];
            }
            else
            {
                Base = DeformInstance->Offset24;
                Add = 0.0f;
                Ref = DeformInstance->PartArray[0].Lod->BoneSlots[0].LocalTransform[10];
            }
            const double Delta = (((double)Base + (double)Add) +
                                  (double)DeformInstance->Offset20) -
                                 (double)Ref;
            if (std::fabs(Delta) > 1.1920928955078125e-07)
            {
                float Vec[3];
                Vec[0] = 0.0f;
                Vec[1] = (float)Delta;
                Vec[2] = 0.0f;
                G_Animation->AddOverlayPos(Handle, Vec);
            }
        }
        Node = Node->Next;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Unused25(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
}

GENRESULT DACOM_API FDeformable::GetListCount(void* Instance, int* Out) // formerly DeformableSlot26
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2170 (RET 0xC), 065f2170..2195: null DeformInstance -> *Out = 0, return -1; else
    // FUN_065f51d0 (FUN_065f4de0 sweep, EAX = +0x14 list count), *Out = EAX, return 0. *Out
    // is never tested.
    if (DeformInstance == nullptr)
    {
        *Out = 0;
        return GR_GENERIC;
    }
    SweepExpiredList(DeformInstance);
    *Out = DeformInstance->ListCount;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::CopyList(void* Instance, int* Out) // formerly DeformableSlot27
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f21a0 → FUN_065f51e0 copy node+4.
    if (DeformInstance == nullptr || Out == nullptr)
        return GR_GENERIC;
    FIkListNode* Node = DeformInstance->ListHead;
    int* Cursor = Out;
    while (Node != nullptr)
    {
        *Cursor = (int)((ptrdiff_t)Node->Event);
        Cursor += 1;
        Node = Node->Next;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Unused28(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused29(void* A, void* B, void* C, void* D, void* E,
                                                 void* F, void* G, void* H, void* I)
{
    return StubRet28(this, A, B, C, D, E, F, G, H, I);
}

GENRESULT DACOM_API FDeformable::Unused30(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused31(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused32(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused33(void* A, void* B, void* C)
{
    return StubRet10(this, A, B, C);
}

GENRESULT DACOM_API FDeformable::Unused34(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
}

GENRESULT DACOM_API FDeformable::GetOffset(void* Instance, float* Out) // formerly DeformableSlot35
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2130 (RET 0xC), 065f2130..2149: null DeformInstance -> -1; else *Out = +0x20 (no
    // test of Out), 0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    *Out = DeformInstance->Offset20;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::WalkHardpoints(void* Instance, void* UserFn, void* UserData) // formerly DeformableSlot36
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f21d0 → FUN_065f59d0 RET 0x8. Wrapper RET 0x10: null inst → -1, else GR_OK.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    WalkInstanceHardpoints(DeformInstance, UserFn, UserData);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetMeshStats(void* Instance, unsigned int* Out) // formerly DeformableSlot37
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f21f0 → FUN_065f5ae0 mesh stats. Mesh+0x10 not required.
    if (DeformInstance == nullptr || Out == nullptr)
        return GR_GENERIC;
    if (DeformInstance->PartCount <= 0)
        return GR_GENERIC;
    Out[0] = (unsigned int)DeformInstance->PartCount;
    Out[1] = 0xFFFFFFFFu;
    Out[2] = 0;
    Out[3] = 0;
    Out[4] = 0;
    Out[5] = 0;
    Out[6] = 0;
    Out[7] = 0;
    Out[8] = 0;
    int Index = 0;
    while (Index < DeformInstance->PartCount)
    {
        FDeformPart* Part = &DeformInstance->PartArray[Index];
        const unsigned int Lod = (unsigned int)Part->LodIndex;
        if (Lod < Out[1])
            Out[1] = Lod;
        Out[6] += (unsigned int)Part->Lod->BindCursor;
        FDeformMesh* Mesh = Part->Current;
        if (Mesh != nullptr)
        {
            // 065f5b58..5b99: slots 1 and 0 through the vtable, then +0x24, +0x40, +0x44 and
            // +0x48 read straight off the object -- for a VMesh wrapper (0x14 bytes) that is
            // past its end, as it is in retail.
            Out[2] += (unsigned int)(Mesh->GetVertexCapacity());
            Out[3] += (unsigned int)(Mesh->GetIndexCount());
            const unsigned int* Raw = (const unsigned int*)Mesh;
            Out[4] += Raw[0x24 / 4];
            Out[5] += Raw[0x40 / 4];
            Out[7] += Raw[0x44 / 4];
            Out[8] += Raw[0x48 / 4];
        }
        Index += 1;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetBoundSphere(void* Instance, float* OutRadius,
                                                      float* OutCenter) // formerly DeformableSlot38
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2220 (RET 0x10): EAX = -1 (ESI); null DeformInstance -> -1; FUN_065f5bc0(OutRadius,
    if (DeformInstance == nullptr || DeformInstance->PartCount <= 0)
        return GR_GENERIC;

    float BestRadius = 0.0f;
    float FirstCenter[3];
    FirstCenter[0] = 0.0f;
    FirstCenter[1] = 0.0f;
    FirstCenter[2] = 0.0f;
    PartBoundSphere(&DeformInstance->PartArray[0], &BestRadius, FirstCenter);

    int Index = 0;
    while (Index < DeformInstance->PartCount)
    {
        float Radius = 0.0f;
        float Center[3];
        Center[0] = 0.0f;
        Center[1] = 0.0f;
        Center[2] = 0.0f;
        PartBoundSphere(&DeformInstance->PartArray[Index], &Radius, Center);
        const double Dx = (double)Center[0] - (double)FirstCenter[0];
        const double Dy = (double)Center[1] - (double)FirstCenter[1];
        const double Dz = (double)Center[2] - (double)FirstCenter[2];
        const double Reach = std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx) + (double)Radius;
        Radius = (float)Reach;
        if (Reach > (double)BestRadius)
            BestRadius = Radius;
        Index += 1;
    }
    if (!(BestRadius > 0.0f))
        return GR_GENERIC;
    OutCenter[0] = FirstCenter[0];
    OutCenter[1] = FirstCenter[1];
    OutCenter[2] = FirstCenter[2];
    *OutRadius = BestRadius;
    return GR_OK;
}

int DACOM_API FDeformable::Initialize()
{
    return (int)(EngineInitialize(this));
}

int DACOM_API FDeformable::CreateArchetype(void* Arg1, void* Arg2)
{
    return (int)(EngineUpdateOk(this, Arg1, Arg2));
}

int DACOM_API FDeformable::DuplicateArchetype(void* Arg1, void* Arg2)
{
    BoneRuntimeRetC(this, Arg1, Arg2);
    return 0;
}

int DACOM_API FDeformable::DestroyArchetype(void* Arg1)
{
    return (int)(BoneRuntimeRet8(this, Arg1));
}

int DACOM_API FDeformable::QueryArchetype(void* Arg1, const char* Iid, void** Out)
{
    // The callback ABI carries the const IID through an opaque mutable token.
    return (int)(EngineStub10(this, Arg1, const_cast<char*>(Iid), Out));
}

int DACOM_API FDeformable::CreateInstance(void* Arg1, void* Arg2)
{
    return (int)(EngineCallbackTrampoline(this, Arg1, Arg2));
}

int DACOM_API FDeformable::DestroyInstance(void* Arg1)
{
    return (int)(BoneRuntimeRet8(this, Arg1));
}

int DACOM_API FDeformable::UpdateInstance(void* Arg1, void* Arg2)
{
    BoneRuntimeRetC(this, Arg1, Arg2);
    return 0;
}

int DACOM_API FDeformable::QueryInstance(void* Arg1, const char* Iid, void** Out)
{
    // The callback ABI carries the const IID through an opaque mutable token.
    return (int)(EngineStub10(this, Arg1, const_cast<char*>(Iid), Out));
}

int DACOM_API FDeformable::Notify(void* Arg1)
{
    return (int)(BoneRuntimeRet8(this, Arg1));
}

// --- factory ---

GENRESULT DACOM_API FDeformableFactory::QueryInterface(const char* Iid, void** Out)
{
    if (Out != nullptr)
        *Out = nullptr;
    if (Iid == nullptr || Out == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    if (!IidEquals(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = this;
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FDeformableFactory::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FDeformableFactory::Release()
{
    if (RefCount != 0)
        RefCount -= 1;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        // Shared factory base has no virtual destructor; derived tails are trivial.
        operator delete(this);
        return 0;
    }
    return Remaining;
}

static GENRESULT DACOM_API FactoryCreateInstance(FDeformableFactory* This, AGGDESC* Desc,
                                                 void** Out)
{
    // FUN_065f22f0 (RET 0xC), 065f22f0..23fb. Size != 0x18 or strcmp(desc+4, factory+8) != 0
    // -> *Out = 0, 0xFFFFFFFD. operator new(0x18) (VC6 MSVCRT: null on failure, tested at
    // 065f2352) null -> *Out = 0, 0xFFFFFFFC. No test of This, Desc, Out or either name.
    FAggDesc18* Extended = (FAggDesc18*)Desc;
    if (Extended->Size != 0x18 || std::strcmp(Extended->NameAt4, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FDeformableObject* Object =
        new (std::nothrow) FDeformableObject();
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    // FUN_065f1810 is FDeformable's constructor (it zeroes the module caches); the final
    Object->Inner.RefCount = 1;                                  // 065f2366
    Object->Inner.Owner = Object;                                // 065f236d
    IDAComponent* const InnerUnknown = &Object->Inner;

    if (Extended->Outer == nullptr)
        Object->Controlling = InnerUnknown;                      // 065f2387
    else
    {
        // 065f23c5: *InnerOut = &inner with no test (only the failure path tests it).
        Object->Controlling = (IDAComponent*)Extended->Outer;
        *Extended->InnerOut = InnerUnknown;
    }

    const GENRESULT InitResult = ApplyCreateInstanceInit(Object, Extended);
    if (InitResult != GR_OK)
    {
        if (Extended->InnerOut != nullptr)
            *Extended->InnerOut = nullptr;
        delete Object;
        *Out = nullptr;
        return InitResult;
    }

    *Out = Object;
    return GR_OK;
}

struct FDeformableComponentFactory : FDeformableFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};

GENRESULT DACOM_API FDeformableComponentFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return FactoryCreateInstance(this, Desc, Out);
}

// Saves one render state on construction and puts it back on destruction: the three
// {pipeline, state, old value} triples LAB_065fdd10 keeps at ESP+0x24 / +0x30 / +0x18 and
// restores in reverse order before it returns (065fdf29..06d).
struct FSavedRenderState
{
    IRenderPipeline* Pipeline;
    D3DRENDERSTATETYPE State;
    int Old;

    FSavedRenderState(IRenderPipeline* InPipeline, D3DRENDERSTATETYPE InState)
        : Pipeline(InPipeline)
        , State(InState)
        , Old(0)
    {
        Pipeline->GetRenderState(State, &Old);
    }
    ~FSavedRenderState()
    {
        Pipeline->SetRenderState(State, Old);
    }
    FSavedRenderState(const FSavedRenderState&) = delete;
    FSavedRenderState& operator=(const FSavedRenderState&) = delete;
};

// DAT_06602a60: 1 in .data and never written (four reads, all in LAB_065fdd10). It gates the
// bone palette and feeds D3DRS_SOFTWAREVERTEXPROCESSING / D3DRS_INDEXEDVERTEXBLENDENABLE and,
// as 3 or 0, D3DRS_VERTEXBLEND.
static unsigned long G_VmeshPaletteSkinning = 1;

// LAB_065fdd10, the VMesh wrapper's slot 5 (thiscall RET 8: Camera, Part), 065fdd10..df7a.
unsigned char FVmeshWrapper::Draw(ICamera* Camera, FDeformPart* Part)
{
    FLodHolder* const Holder = Part->Lod;
    const float* const View = Camera->GetViewTransform();
    int bHaveView = 1;
    G_Pipeline->GetPipelineState(7, &bHaveView);
    if (VMeshPart == nullptr)
        return 1;
    if (G_VmeshPaletteSkinning != 0 && Holder->BindCursor > 0)
    {
        int WorldIndex = 1;
        do
        {
            FBoneRuntime* const Bone = &Holder->BoneSlots[WorldIndex - 1];
            if (bHaveView != 0)
            {
                float Composed[12];
                AcquireMathEngine()->MulTransform(*(FTransform*)Composed,
                                                  *(const FTransform*)View,
                                                  *(const FTransform*)
                                                      Bone->Matrix); // +0x1C
                float Copy[12];
                VmeshCopy12(Copy, Composed);
                G_Pipeline->SetWorldN(WorldIndex, Copy);
            }
            else
                G_Pipeline->SetWorldN(WorldIndex, Bone->Matrix);
            WorldIndex += 1;
        } while (WorldIndex - 1 < Holder->BindCursor);
    }

    FSavedRenderState SoftwareVertexProcessing(G_Pipeline, D3DRS_SOFTWAREVERTEXPROCESSING);
    G_Pipeline->SetRenderState(D3DRS_SOFTWAREVERTEXPROCESSING, (int)G_VmeshPaletteSkinning);
    FSavedRenderState IndexedVertexBlend(G_Pipeline, D3DRS_INDEXEDVERTEXBLENDENABLE);
    G_Pipeline->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, (int)G_VmeshPaletteSkinning);
    FSavedRenderState VertexBlend(G_Pipeline, D3DRS_VERTEXBLEND);
    G_Pipeline->SetRenderState(D3DRS_VERTEXBLEND,
                              G_VmeshPaletteSkinning != 0 ? D3DVBF_3WEIGHTS : D3DVBF_DISABLE);
    VMeshPart->Draw(Camera, Holder->BoneSlots[0].Instance, 0, nullptr, nullptr);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        // FUN_065f2250 (RET 0xC), 065f2250..22b8: reason 1 -> DisableThreadLibraryCalls,
        DisableThreadLibraryCalls(Instance);
        ICOManager* Dacom = DACOM_Acquire();

        FDeformableComponentFactory* Factory = new FDeformableComponentFactory();
        Factory->RefCount = 1;
        Factory->Name = G_DeformableComponentName;
        Dacom->RegisterComponent((IDAComponent*)Factory, G_DeformableComponentName,
                                 (void*)0x80000000);
        Factory->Release();
        Dacom->Release();
    }
    return TRUE;
}
