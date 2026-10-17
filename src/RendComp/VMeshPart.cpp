// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RendComp.h"
#include "common_misc.h"
#include "render/shading.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// VMPartArch.cpp (06c308c0..06c31e35) and the VMesh render plugin (06c2d710..06c2dd19) of
// retail rendcomp.dll.

class FVMeshPart;

// +0x04: IMesh face (PTR_LAB_06c36638, 30 slots).
class FVMeshPartMesh : public IMesh
{
public:
    FVMeshPart* Owner;

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    int DACOM_API ResetMesh() override; // formerly Slot3
    int DACOM_API NoOp() override; // formerly Slot4
    int DACOM_API RefreshMaterials(unsigned long Flags) override; // formerly Slot5
    int DACOM_API RejectOperation(void* A) override; // formerly Slot6
    int DACOM_API RejectOperation() override; // formerly Slot7
    int DACOM_API GetFaceGroupCount(int* Out) override; // formerly Slot8
    int DACOM_API OpenFaceGroup(int Index, int Mode) override; // formerly Slot9
    int DACOM_API CloseFaceGroup() override; // formerly Slot10
    int DACOM_API AddFaceGroup(void* A) override; // formerly Slot11
    int DACOM_API RemoveFaceGroup(int Index) override; // formerly Slot12
    int DACOM_API GetFaceGroupPrimitiveType(unsigned int* Out) override; // formerly Slot13
    int DACOM_API GetFaceGroupMaterial(IMaterial** Out) override; // formerly Slot14
    int DACOM_API SetFaceGroupMaterial(IMaterial* Material) override; // formerly Slot15
    int DACOM_API GetFaceGroupMaterialContext(void** Out) override; // formerly Slot16
    int DACOM_API SetFaceGroupMaterialContext(void* A) override; // formerly Slot17
    int DACOM_API OpenFaceGroupChannels(unsigned long Flags) override; // formerly Slot18
    int DACOM_API CloseFaceGroupChannels() override; // formerly Slot19
    int DACOM_API LockFaceGroupChannels(unsigned long Mask) override; // formerly Slot20
    int DACOM_API UnlockFaceGroupChannels() override; // formerly Slot21
    int DACOM_API GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D) override; // formerly Slot22
    int DACOM_API SetFaceGroupData(int Channel, void* A, void* B, void* C, void* D) override; // formerly Slot23
    int DACOM_API GetFaceGroupIndices(int Channel, void** A, void** B) override; // formerly Slot24
    int DACOM_API SetFaceGroupIndices(int Channel, void* Src, int Count) override; // formerly Slot25
    int DACOM_API GetFaceGroupIndexCount(int Selector, void** Out) override; // formerly Slot26
    int DACOM_API SetFaceGroupIndexCount(int Selector, int Count) override; // formerly Slot27
    int DACOM_API AddFaceGroupChannels(unsigned long Flags) override; // formerly Slot28
    int DACOM_API RemoveFaceGroupChannels(unsigned long Flags) override; // formerly Slot29
};

// +0x08: IMatHookUp face (PTR_LAB_06c36628, four entries).
class FVMeshPartHook : public IMatHookUp
{
public:
    FVMeshPart* Owner;

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    int DACOM_API HookUpMaterials() override; // formerly Slot3
};

// +0x0C: IRenderableMesh face (PTR_LAB_06c3660c).
class FVMeshPartRenderable : public IRenderableMesh
{
public:
    FVMeshPart* Owner;

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    int DACOM_API GetVertexCount(int* Out) override; // formerly Slot3
    int DACOM_API CopyVertexData(void* Dest, unsigned int Fvf, unsigned int StartVertex, unsigned int VertexCount) override; // formerly Slot4
    int DACOM_API GetIndexCount(int* Out) override; // formerly Slot5
    int DACOM_API CopyIndices(unsigned short** Dest, int Count) override; // formerly Slot6
};

// The VMesh part archetype (retail 0x90: four vtables, Cursor +0x10, UseCount +0x14,
// Data +0x18, Ref +0x1C, 0x70 vertex descriptor +0x20). The plugin hands its address out as
// the LOD handle.
class FVMeshPart : public IVMeshPart
{
public:
    FVMeshPartMesh Mesh;
    FVMeshPartHook Hook;
    FVMeshPartRenderable Renderable;
    int Cursor;                 // +0x10 selected facegroup, -1 = none
    int UseCount;               // +0x14 IRenderComponent Slot7/Slot8
    FVmeshData* Data;           // +0x18
    FVmeshRef* Ref;             // +0x1C the VMeshRef leaf, owned
    FRpVertexSrcDesc VertexDesc;       // +0x20 FUN_06c31730, built by the first HookUp

    FVMeshPart();
    ~FVMeshPart();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    bool DACOM_API Draw(ICamera* Camera, void* Inst, unsigned long Flags, const void* Matrix,
                        IMaterial* Override) override;
    bool DACOM_API Prepare(void* Listing, void* Scratch) override;
    FVmeshRef* DACOM_API GetRef() override;
    FVmeshDataHeader* DACOM_API GetHeader() override;

    int Claim(IFileSystem* File);
    int HookUp();
    bool ChannelPresent(unsigned int ChannelId) const;
    const FVmeshFacegroup* Facegroups() const;
    int TriangleCount() const;
};

// FUN_06c30920: vtables, Data 0, UseCount 0, Cursor -1, Ref 0, then the no-op FUN_06c2f5e0.
// The vertex descriptor is left as allocated.
FVMeshPart::FVMeshPart()
    : Cursor(-1)
    , UseCount(0)
    , Data(nullptr)
    , Ref(nullptr)
{
    Mesh.Owner = this;
    Hook.Owner = this;
    Renderable.Owner = this;
}

FVMeshPart::~FVMeshPart()
{
    Mesh.CloseFaceGroup();
    if (Data != nullptr)
    {
        for (unsigned int Index = 0; Index < Ref->FacegroupCount; ++Index)
        {
            IMaterial** Slot = &Data->Materials[Ref->FacegroupStart + Index];
            if (*Slot != nullptr)
            {
                (*Slot)->Release();
                *Slot = nullptr;
            }
        }
        if (Data != nullptr)
        {
            VMeshDataRelease(Data);
            Data = nullptr;
        }
    }
    delete[] (const unsigned char*)Ref;
    Ref = nullptr;
    if (Data != nullptr)
    {
        VMeshDataRelease(Data);
        Data = nullptr;
    }
}

// LAB_06c31d10: exact compares, no AddRef. "1.11_IVMeshLibrary" -> this; IMesh -> +4;
// IMatHookUp -> +8; IRenderableMesh -> +0xC; IDAComponent -> +4. A miss returns -1 and leaves
// Out alone. (Retail also writes 0 and returns 0 for a null this, which C++ cannot reach.)
GENRESULT DACOM_API FVMeshPart::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iid, Iids::VMeshLibrary) == 0)
    {
        *Out = (IVMeshPart*)this;
        return 0;
    }
    if (std::strcmp(Iid, Iids::Mesh) == 0)
    {
        *Out = (IMesh*)(&Mesh);
        return 0;
    }
    if (std::strcmp(Iid, Iids::MatHookUp) == 0)
    {
        *Out = (IMatHookUp*)(&Hook);
        return 0;
    }
    if (std::strcmp(Iid, Iids::RenderableMesh) == 0)
    {
        *Out = (IRenderableMesh*)(&Renderable);
        return 0;
    }
    if (std::strcmp(Iid, Iids::DAComponent) == 0)
    {
        *Out = (IMesh*)(&Mesh);
        return 0;
    }
    return -1;
}

// FUN_06c2b100: MOV EAX,1 -- the part is not reference counted.
unsigned long DACOM_API FVMeshPart::AddRef()
{
    return 1;
}

unsigned long DACOM_API FVMeshPart::Release()
{
    return 1;
}

// FUN_06c30900.
FVmeshRef* DACOM_API FVMeshPart::GetRef()
{
    return Ref;
}

// FUN_06c30910.
FVmeshDataHeader* DACOM_API FVMeshPart::GetHeader()
{
    if (Data == nullptr)
        return nullptr;
    return Data->Header;
}

const FVmeshFacegroup* FVMeshPart::Facegroups() const
{
    return (const FVmeshFacegroup*)(
        (const unsigned char*)Data->Header + 0x10);
}

// FUN_06c31160: the channel test on the VMeshData header FVF.
bool FVMeshPart::ChannelPresent(unsigned int ChannelId) const
{
    return VMeshChannelPresent(Data->Header->Fvf, ChannelId);
}

// FUN_06c31110: sum of IndexCount / 3 (signed division) over the part's facegroups.
int FVMeshPart::TriangleCount() const
{
    int Total = 0;
    const FVmeshFacegroup* Facegroup = Facegroups() + Ref->FacegroupStart;
    for (int Remaining = Ref->FacegroupCount; Remaining > 0; --Remaining)
    {
        Total += (int)Facegroup->IndexCount / 3;
        Facegroup += 1;
    }
    return Total;
}

int FVMeshPart::Claim(IFileSystem* File)
{
    FFileSystemDesc Desc;
    std::memset(&Desc, 0, sizeof(Desc));
    Desc.ComponentName = "FileSystem";
    Desc.Access = 0x80000000;
    Desc.ShareMode = 1;
    Desc.Creation = 3;
    Desc.Flags = 0x8000080;
    Desc.Path = "VMeshRef";
    Desc.Size = 0x34;
    Desc.Field30 = 0xFFFFFFFF;
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        const unsigned long Size = File->GetSize((unsigned long)Handle, 0);
        FVmeshRef* Buffer = (FVmeshRef*)new unsigned char[(Size / sizeof(FVmeshRef)) * sizeof(FVmeshRef)];
        unsigned long ReadCount = 0;
        if (File->Read((unsigned long)Handle, Buffer, Size, &ReadCount, 0) != 0)
            Ref = Buffer;
        else
            delete[] (const unsigned char*)Buffer;
        File->CloseFile((unsigned long)Handle);
    }
    Hook.HookUpMaterials();
    return 0;
}

int FVMeshPart::HookUp()
{
    unsigned char bFailed = 0;
    if (Data == nullptr)
    {
        FVmeshKey Key;
        Key.Name = nullptr;
        Key.Crc = Ref->Crc;
        FVmeshData* Found = VMeshDataFind(&Key);
        if (Data != Found)
        {
            if (Data != nullptr)
            {
                VMeshDataRelease(Data);
                Data = nullptr;
            }
            Data = Found;
            if (Found != nullptr)
                VMeshDataAddRef(Found);
        }
        if (Data == nullptr)
            return -1;
        VMeshBuildVertexDesc(Data->Header, Ref->VertexStart, Ref->VertexCount, &VertexDesc);
    }
    if (G_MaterialLibrary == nullptr)
        return -1;

    IMaterialLibrary* Library = G_MaterialLibrary;
    for (unsigned int Index = 0; Index < Ref->FacegroupCount; ++Index)
    {
        const unsigned int GroupIndex = Ref->FacegroupStart + Index;
        const FVmeshFacegroup* Facegroup = Facegroups() + GroupIndex;
        IMaterial** Slot = &Data->Materials[GroupIndex];
        IMaterial* Previous = *Slot;
        const unsigned long Crc = Facegroup->MaterialCrc;
        bool bFound = false;
        if (Crc != 0)
        {
            unsigned long Query[2];
            Query[0] = 0;
            Query[1] = Crc;
            bFound = Library->FindMaterialByCrc(Query, Slot) >= 0;
        }
        if (bFound)
        {
            if (Previous != nullptr)
                Previous->Release();
        }
        else
        {
            char Message[0x2000];
            std::sprintf(Message, " VMESH: couldnt find material %u", Facegroup->MaterialCrc);
            RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x2B9, Message);
            *Slot = Previous;
            bFailed = 1;
        }
    }
    if (bFailed != 0)
        return (int)0xFFFFFFF8;
    return 0;
}

// FUN_06c31950 (thiscall, RET 8) -> AL. The group's VB must select (FUN_06c2d4a0); the
// listing gets {VB, FVF, base vertex + VMeshRef.VertexStart}; the IB is made current with
// that base vertex (FUN_06c2d5a0) and the scratch gets {IB, start index + IndexStart}.
bool DACOM_API FVMeshPart::Prepare(void* ListingArg, void* ScratchArg)
{
    FVbListing* Listing = (FVbListing*)ListingArg;
    FIbScratch* Scratch = (FIbScratch*)ScratchArg;
    int Result = -1;
    if (Data == nullptr || Data->Group == nullptr)
        return Result >= 0;
    FVmeshGroup* Group = Data->Group;
    if (VMeshGroupEnsureVb(Group) < 0)
        return false;
    Listing->VertexBuffer = Data->VertexBuffer;
    Listing->Fvf = Data->Fvf;
    Listing->BaseVertex = Data->BaseVertex;
    Listing->BaseVertex += Ref->VertexStart;
    Result = VMeshGroupEnsureIb(Group, Listing->BaseVertex);
    if (Result >= 0)
    {
        Scratch->IndexBuffer = Data->IndexBuffer;
        Scratch->Bias = Data->StartIndex;
        Scratch->Bias += Ref->IndexStart;
    }
    return Result >= 0;
}

bool DACOM_API FVMeshPart::Draw(ICamera* Camera, void* Inst, unsigned long Flags,
                                const void* Matrix, IMaterial* Override)
{
    if (Data == nullptr)
        return false;
    const FTransform* View = (const FTransform*)(Camera->GetViewTransform());
    const FTransform* World = (const FTransform*)(G_Engine->GetInstanceTransform(Inst));
    const FTransform* MatrixXf = (const FTransform*)Matrix;
    if ((Flags & 0x200) == 0)
    {
        int State = 1;
        G_Pipeline->GetPipelineState(7, &State);
        if (State != 0)
        {
            FTransform Product;
            if (MatrixXf != nullptr)
            {
                FTransform Local;
                MathEngineCached(G_MathVMesh)->MulTransform(Local, *MatrixXf, *World);
                const FTransform LocalCopy = Local;
                MathEngineCached(G_MathVMesh)->MulTransform(Product, *View, LocalCopy);
            }
            else
                MathEngineCached(G_MathVMesh)->MulTransform(Product, *View, *World);
            const FTransform ProductCopy = Product;
            const FTransform Modelview = ProductCopy;
            G_Pipeline->SetModelview((const float*)(&Modelview));
        }
        else if (MatrixXf != nullptr)
        {
            FTransform Local;
            MathEngineCached(G_MathVMesh)->MulTransform(Local, *MatrixXf, *World);
            const FTransform Unused = Local;
            (void)Unused;
            G_Pipeline->SetWorld((const float*)World);
        }
        else
        {
            G_Pipeline->SetWorld((const float*)World);
            G_Pipeline->SetView((const float*)View);
        }
    }
    Flags |= 0x10C;

    const FVmeshDataHeader* Header = Data->Header;
    FVbListing Listing;
    FIbScratch Scratch;
    FMaterialSubmitContext Context;
    Context.Inst = Inst;
    Context.World = (FTransform*)World;
    Context.View = (FTransform*)View;
    Context.SourceVertices = &VertexDesc;
    Context.Indices = (unsigned short*)(
        (unsigned char*)((const unsigned char*)Header) + 0x10 +
        (unsigned long)Header->FaceGroups * 0xC +
        (unsigned long)Ref->IndexStart * 2);
    Context.VbListing = &Listing;
    Context.IbScratch = &Scratch;
    Context.Field20 = 0;

    unsigned long IndexStart = 0;
    const unsigned int FacegroupCount = Ref->FacegroupCount;
    for (unsigned int Index = 0; Index < FacegroupCount; ++Index)
    {
        const unsigned int GroupIndex = Ref->FacegroupStart + Index;
        const FVmeshFacegroup* Facegroup = Facegroups() + GroupIndex;
        IMaterial* Material = Data->Materials[GroupIndex];
        Context.ReplacedMaterial = nullptr;
        if (Override != nullptr)
        {
            Context.ReplacedMaterial = Material;
            Material = Override;
        }
        if (Material != nullptr)
        {
            if (!Prepare(&Listing, &Scratch))
                return false;
            Material->Bind(&Context);
            const unsigned short VertexCount =
                (unsigned short)(Facegroup->VertexEnd - Facegroup->VertexBegin + 1);
            Material->Submit(&Context, Header->PrimitiveType, Facegroup->VertexBegin, VertexCount,
                             IndexStart, Facegroup->IndexCount, Flags);
            Material->Teardown(&Context);
        }
        IndexStart += Facegroup->IndexCount;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// IMesh face. Every body reaches the part through Owner (retail: this - 4).
// ---------------------------------------------------------------------------------------

// LAB_06c31e00: SUB [ESP+4],4; JMP LAB_06c31d10.
GENRESULT DACOM_API FVMeshPartMesh::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

// LAB_06c27670 -> FUN_06c2b100.
unsigned long DACOM_API FVMeshPartMesh::AddRef()
{
    return Owner->AddRef();
}

unsigned long DACOM_API FVMeshPartMesh::Release()
{
    return Owner->Release();
}

// FUN_06c2f5e0 (slots 3 and 4).
// formerly Slot3
int DACOM_API FVMeshPartMesh::ResetMesh()
{
    return 0;
}

// formerly Slot4
int DACOM_API FVMeshPartMesh::NoOp()
{
    return 0;
}

// LAB_06c2c2d0.
// formerly Slot5
int DACOM_API FVMeshPartMesh::RefreshMaterials(unsigned long /*Flags*/)
{
    return 0;
}

// LAB_06c22650.
// formerly Slot6
int DACOM_API FVMeshPartMesh::RejectOperation(void* /*A*/)
{
    return -1;
}

// LAB_06c22660.
// formerly Slot7
int DACOM_API FVMeshPartMesh::RejectOperation()
{
    return -1;
}

// LAB_06c30a00: facegroup count of the part.
// formerly Slot8
int DACOM_API FVMeshPartMesh::GetFaceGroupCount(int* Out)
{
    *Out = Owner->Ref->FacegroupCount;
    return 0;
}

// LAB_06c30a20: select a facegroup; a selection already open, or an index past the part's
// count (unsigned), -> -1.
// formerly Slot9
int DACOM_API FVMeshPartMesh::OpenFaceGroup(int Index, int /*Mode*/)
{
    if (Owner->Cursor >= 0)
        return -1;
    if ((unsigned int)Index >= Owner->Ref->FacegroupCount)
        return -1;
    Owner->Cursor = Index;
    return 0;
}

// LAB_06c30a50: close the selection through the own vtable's slots 19 and 21.
// formerly Slot10
int DACOM_API FVMeshPartMesh::CloseFaceGroup()
{
    if (Owner->Cursor < 0)
        return -1;
    CloseFaceGroupChannels();
    UnlockFaceGroupChannels();
    Owner->Cursor = -1;
    return 0;
}

// LAB_06c30a80.
// formerly Slot11
int DACOM_API FVMeshPartMesh::AddFaceGroup(void* /*A*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x9E,
                    "VMPartArch add_facegroup: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30ab0.
// formerly Slot12
int DACOM_API FVMeshPartMesh::RemoveFaceGroup(int /*Index*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0xA5,
                    "VMPartArch remove_facegroup: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30ae0: the dword at VMeshData header +4.
// formerly Slot13
int DACOM_API FVMeshPartMesh::GetFaceGroupPrimitiveType(unsigned int* Out)
{
    if (Owner->Cursor < 0)
        return -1;
    *Out = Owner->Data->Header->PrimitiveType;
    return 0;
}

// LAB_06c30b10: the selected facegroup's material, AddRef'd when present.
// formerly Slot14
int DACOM_API FVMeshPartMesh::GetFaceGroupMaterial(IMaterial** Out)
{
    if (Owner->Cursor < 0)
        return -1;
    IMaterial* Material =
        Owner->Data->Materials[Owner->Ref->FacegroupStart + (unsigned int)Owner->Cursor];
    *Out = Material;
    if (Material != nullptr)
    {
        Material->AddRef();
        return 0;
    }
    *Out = nullptr;
    return 0;
}

// LAB_06c30b60.
// formerly Slot15
int DACOM_API FVMeshPartMesh::SetFaceGroupMaterial(IMaterial* /*Material*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0xD2,
                    "VMPartArch set_facegroup_material: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30b90.
// formerly Slot16
int DACOM_API FVMeshPartMesh::GetFaceGroupMaterialContext(void** /*Out*/)
{
    if (Owner->Cursor < 0)
        return -1;
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0xE0,
                    "VMPartArch get_facegroup_material_context: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30bd0.
// formerly Slot17
int DACOM_API FVMeshPartMesh::SetFaceGroupMaterialContext(void* /*A*/)
{
    if (Owner->Cursor < 0)
        return -1;
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0xEE,
                    "VMPartArch set_facegroup_material_context: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30c10 (slots 18 and 20): channel present -> 0, absent -> -8.
// formerly Slot18
int DACOM_API FVMeshPartMesh::OpenFaceGroupChannels(unsigned long Flags)
{
    if (Owner->Cursor < 0)
        return -1;
    return Owner->ChannelPresent((unsigned int)Flags) ? 0 : -8;
}

// LAB_06c30c40 (slots 19 and 21).
// formerly Slot19
int DACOM_API FVMeshPartMesh::CloseFaceGroupChannels()
{
    return Owner->Cursor >= 0 ? 0 : -1;
}

// formerly Slot20
int DACOM_API FVMeshPartMesh::LockFaceGroupChannels(unsigned long Mask)
{
    if (Owner->Cursor < 0)
        return -1;
    return Owner->ChannelPresent((unsigned int)Mask) ? 0 : -8;
}

// formerly Slot21
int DACOM_API FVMeshPartMesh::UnlockFaceGroupChannels()
{
    return Owner->Cursor >= 0 ? 0 : -1;
}

// LAB_06c30c60: FUN_06c31500 record over the part's vertex range -> *A pointer, *B stride,
// *C element size, *D vertex count.
// formerly Slot22
int DACOM_API FVMeshPartMesh::GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D)
{
    if (Owner->Cursor < 0)
        return -1;
    const unsigned int ChannelId = (unsigned int)Channel;
    if (!Owner->ChannelPresent(ChannelId))
        return -1;
    FVmeshChannelRecord Record;
    VMeshFillChannelRecord(Owner->Data->Header, Owner->Ref->VertexStart, Owner->Ref->VertexCount,
                           ChannelId, &Record);
    *A = Record.Pointer;
    *B = (void*)((intptr_t)Record.Stride);
    *D = (void*)((intptr_t)Record.VertexCount);
    *C = (void*)((intptr_t)Record.ElemBytes);
    return 0;
}

// LAB_06c30ce0.
// formerly Slot23
int DACOM_API FVMeshPartMesh::SetFaceGroupData(int /*Channel*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x13F,
                    "VMPartArch set_facegroup_data: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30d90: *B = the selected facegroup's IndexCount; *A = header + 0x10 + 12 * FaceGroups
// + 6 * (FacegroupStart + Cursor) -- retail scales the facegroup index by 6 bytes here.
// formerly Slot24
int DACOM_API FVMeshPartMesh::GetFaceGroupIndices(int /*Channel*/, void** A, void** B)
{
    if (Owner->Cursor < 0)
        return -1;
    const unsigned int GroupIndex =
        Owner->Ref->FacegroupStart + (unsigned int)Owner->Cursor;
    const FVmeshDataHeader* Header = Owner->Data->Header;
    *B = (void*)((uintptr_t)(Owner->Facegroups()[GroupIndex].IndexCount));
    const unsigned int Scaled =
        Owner->Ref->FacegroupStart + (unsigned int)Header->FaceGroups * 2 +
        (unsigned int)Owner->Cursor;
    *A = (unsigned char*)((const unsigned char*)Header) + 0x10 +
         Scaled * 6;
    return 0;
}

// LAB_06c30df0.
// formerly Slot25
int DACOM_API FVMeshPartMesh::SetFaceGroupIndices(int /*Channel*/, void* /*Src*/, int /*Count*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x188,
                    "VMPartArch set_facegroup_indices: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30d10: selector 1 only -> the selected facegroup's IndexCount.
// formerly Slot26
int DACOM_API FVMeshPartMesh::GetFaceGroupIndexCount(int Selector, void** Out)
{
    if (Owner->Cursor < 0)
        return -1;
    if (Selector != 1)
        return -1;
    const unsigned int GroupIndex =
        Owner->Ref->FacegroupStart + (unsigned int)Owner->Cursor;
    *Out = (void*)((uintptr_t)(Owner->Facegroups()[GroupIndex].IndexCount));
    return 0;
}

// LAB_06c30d60.
// formerly Slot27
int DACOM_API FVMeshPartMesh::SetFaceGroupIndexCount(int /*Selector*/, int /*Count*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x165,
                    "VMPartArch set_facegroup_indices_count: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30e20.
// formerly Slot28
int DACOM_API FVMeshPartMesh::AddFaceGroupChannels(unsigned long /*Flags*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x18F,
                    "VMPartArch add_elements: not implemented");
    return (int)0xFFFFFFF9;
}

// LAB_06c30e50.
// formerly Slot29
int DACOM_API FVMeshPartMesh::RemoveFaceGroupChannels(unsigned long /*Flags*/)
{
    RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x196,
                    "VMPartArch remove_elements: not implemented");
    return (int)0xFFFFFFF9;
}

// ---------------------------------------------------------------------------------------
// IMatHookUp face.
// ---------------------------------------------------------------------------------------

// LAB_06c31e10: SUB [ESP+4],8; JMP LAB_06c31d10.
GENRESULT DACOM_API FVMeshPartHook::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

// LAB_06c27690 -> FUN_06c2b100.
unsigned long DACOM_API FVMeshPartHook::AddRef()
{
    return Owner->AddRef();
}

unsigned long DACOM_API FVMeshPartHook::Release()
{
    return Owner->Release();
}

// LAB_06c317b0.
// formerly Slot3
int DACOM_API FVMeshPartHook::HookUpMaterials()
{
    return Owner->HookUp();
}

// ---------------------------------------------------------------------------------------
// IRenderableMesh face.
// ---------------------------------------------------------------------------------------

// LAB_06c31e20: SUB [ESP+4],0xC; JMP LAB_06c31d10.
GENRESULT DACOM_API FVMeshPartRenderable::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

// LAB_06c31e30 -> FUN_06c2b100.
unsigned long DACOM_API FVMeshPartRenderable::AddRef()
{
    return Owner->AddRef();
}

unsigned long DACOM_API FVMeshPartRenderable::Release()
{
    return Owner->Release();
}

// LAB_06c30e80: VMeshRef.VertexCount.
// formerly Slot3
int DACOM_API FVMeshPartRenderable::GetVertexCount(int* Out)
{
    *Out = Owner->Ref->VertexCount;
    return 0;
}

// formerly Slot4
int DACOM_API FVMeshPartRenderable::CopyVertexData(void* Dest, unsigned int Fvf, unsigned int StartVertex, unsigned int VertexCount)
{
    IVertexBufferManager* Manager = nullptr;
    int Result = -1;
    if (G_Pipeline->QueryInterface(Iids::VertexBufferManager,
                                   (void**)(&Manager)) >= 0)
        Result = Manager->CopyVertexData(Dest, Fvf, &Owner->VertexDesc, StartVertex, VertexCount);
    if (Manager != nullptr)
        Manager->Release();
    return Result;
}

// LAB_06c30f70: VMeshRef +0x0E.
// formerly Slot5
int DACOM_API FVMeshPartRenderable::GetIndexCount(int* Out)
{
    *Out = Owner->Ref->IndexCount;
    return 0;
}

// formerly Slot6
int DACOM_API FVMeshPartRenderable::CopyIndices(unsigned short** Dest, int Count)
{
    const unsigned int Expected = Owner->Ref->IndexCount;
    if ((unsigned int)Count != Expected)
        return -1;
    const FVmeshDataHeader* Header = Owner->Data->Header;
    const unsigned int Scaled =
        Owner->Ref->FacegroupStart + (unsigned int)Header->FaceGroups * 2;
    const unsigned char* Source = (const unsigned char*)Header + 0x10 + Scaled * 6;
    std::memcpy(*Dest, Source, (size_t)Expected * 2);
    return 0;
}

// VMesh render plugin.
static bool VMeshHandleValid(void* Handle)
{
    return Handle != (void*)G_HandleFail && Handle != nullptr;
}

class FVMeshPlugin : public IRenderComponent
{
public:
    FVMeshPart* Bound;          // +0x04 FUN_06c2d710 zeroes it, Slot17 sets it
    unsigned long Field08;      // +0x08 TODO: calloc-zeroed, no reader in 06c2d710..06c2dd19
    unsigned long RefCount;     // +0x0C

    // LAB_06c2dc30: table {"1.11_IRenderComponent", 0}; miss -> 0, -3.
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        if (std::strcmp(Iids::RenderComponent, Iid) != 0)
        {
            *Out = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }
        AddRef();
        *Out = (IRenderComponent*)this;
        return GR_OK;
    }

    // LAB_06c2dce0.
    unsigned long DACOM_API AddRef() override
    {
        RefCount += 1;
        return RefCount;
    }

    // LAB_06c2dcf0: decrement when above 0; at 0 pin the count at 1 and free() the object.
    unsigned long DACOM_API Release() override
    {
        if (RefCount > 0)
            RefCount -= 1;
        if (RefCount != 0)
            return RefCount;
        RefCount = 1;
        std::free(this);
        return 0;
    }

    int DACOM_API Claim(void* FileArg) override
    {
        IFileSystem* File = (IFileSystem*)FileArg;
        int Handle = -1;
        if (File->OpenNode("VMeshPart") == 0)
            return Handle;
        // Retail calls the claim on a null part when operator new(0x90) fails; the throwing
        // new is the C++ form of that (no null test either way).
        FVMeshPart* Part = new FVMeshPart();
        if (Part->Claim(File) < 0)
        {
            delete Part;
            File->OpenNode(".."); // 06c2d799: DAT_06c37250 = ".." (2E 2E 00)
            return Handle;
        }
        Handle = (int)((intptr_t)Part);
        File->OpenNode(".."); // 06c2d7c8
        return Handle;
    }

    // LAB_06c2d7e0: FUN_06c31070 (clone, not implemented: NOTICE 0x211, -7) -> -1.
    // formerly Slot4
    int DACOM_API CloneHandle(void* /*Handle*/) override
    {
        void* Clone = nullptr;
        RendCompFdumpAt(kFdumpNotice, G_FileVMPartArch, 0x211,
                        "VMPartArch clone: not implemented");
        const int Result = (int)0xFFFFFFF9;
        if (Result < 0 || Clone == nullptr)
            return -1;
        return (int)((intptr_t)Clone);
    }

    // FUN_06c2d820 (RET 8): a valid handle is destroyed. EAX is not set in retail; 0 here.
    int DACOM_API DestroyHandle(void* Handle) override
    {
        if (VMeshHandleValid(Handle))
            delete (FVMeshPart*)Handle;
        return 0;
    }

    // LAB_06c219d0: invalid handle -> -1, else the part's QueryInterface.
    // formerly Slot6
    int DACOM_API QueryHandle(void* Handle, const char* Iid, void** Out) override
    {
        if (!VMeshHandleValid(Handle))
            return -1;
        return ((FVMeshPart*)Handle)->QueryInterface(Iid, Out);
    }

    // formerly Slot7
    int DACOM_API CreateMeshInstance(void* InstId, void* Handle) override
    {
        if (!VMeshHandleValid(Handle))
            return 0;
        FVMeshPart* Part = (FVMeshPart*)Handle;
        Part->UseCount += 1;
        Part->AddRef();
        // Retail passes the Handle argument's own stack slot as the radius out-parameter
        // (LEA EDX,[ESP+0x24] @ 06c2d958) and reads it back with FLD; the start value is the
        // handle's bits.
        float Radius;
        const uint32_t HandleBits = (uint32_t)((uintptr_t)Handle);
        std::memcpy(&Radius, &HandleBits, sizeof(Radius));
        float Center[3];
        G_Engine->GetInstanceExtent(InstId, 1, &Radius, Center);
        if (Radius <= 0.0f)
        {
            Center[0] = Part->Ref->Extra2C[0];
            Center[1] = Part->Ref->Extra2C[1];
            Center[2] = Part->Ref->Extra2C[2];
            Radius = Part->Ref->Extra38;
            G_Engine->SetInstanceExtent(InstId, 1, Radius, Center);
        }
        return 1;
    }

    // formerly Slot8
    int DACOM_API DestroyMeshInstance(void* /*InstId*/, void* Handle) override
    {
        if (!VMeshHandleValid(Handle))
            return 0;
        FVMeshPart* Part = (FVMeshPart*)Handle;
        Part->UseCount -= 1;
        if (Part->UseCount <= 0 && Part->Data != nullptr && Part->Data->Group != nullptr)
        {
            VMeshGroupDestroyVb(Part->Data->Group);
            VMeshGroupDestroyIb(Part->Data->Group);
        }
        return (int)(Part->Release());
    }

    // LAB_06c2da60: a bare RET 0xC (EAX undefined); 0 here.
    // formerly Slot9
    int DACOM_API UpdateMeshInstance(void* /*InstId*/, void* /*Arg*/) override
    {
        return 0;
    }

    // LAB_06c2daa0 (RET 0x14): this->Slot6(Handle, Iid, Out); InstId unread.
    // formerly Slot10
    int DACOM_API QueryMeshInstance(void* /*InstId*/, void* Handle, const char* Iid, void** Out) override
    {
        return QueryHandle(Handle, Iid, Out);
    }

    // FUN_06c2fb50: a bare RET 8; 0 here.
    // formerly Slot11
    int DACOM_API UpdateMeshInstances(void* /*InstId*/) override
    {
        return 0;
    }

    // formerly Slot12
    int DACOM_API QueryGeometry(void* Handle, int /*Lod*/, void* A, void* B, void* /*C*/, void* D,
                         void* /*E*/) override
    {
        if (!VMeshHandleValid(Handle))
            return 0;
        FVMeshPart* Part = (FVMeshPart*)Handle;
        if (Part->Data == nullptr)
            return 0;
        const int Mode = (int)((intptr_t)A);
        const unsigned long Flags = (unsigned long)((uintptr_t)B);
        int* Out = (int*)D;
        if (Mode != 0 && Mode != 2 && Mode != 3)
            return 0;
        if ((Flags & 0xFFF) != 3 || (Flags & 0x4000) == 0 || Out == nullptr)
            return 0;
        if (Mode == 3)
            *Out = Part->Ref->VertexCount;
        else
            *Out = Part->TriangleCount();
        return 1;
    }

    // LAB_06c2d8f0 (RET 0x10) -> AL: FUN_06c310a0 copies VMeshRef +0x14 (6 dwords).
    // formerly Slot13
    int DACOM_API GetBounds(void* Handle, float /*Lod*/, void* Out) override
    {
        if (!VMeshHandleValid(Handle))
            return 0;
        std::memcpy(Out, ((FVMeshPart*)Handle)->Ref->Box14, 6 * sizeof(unsigned long));
        return 1;
    }

    // LAB_06c2da70 (RET 0x10) -> AL: FUN_06c310f0 copies VMeshRef +0x2C (3 floats).
    // formerly Slot14
    int DACOM_API GetCentroid(void* Handle, float /*Lod*/, void* Out) override
    {
        if (!VMeshHandleValid(Handle))
            return 0;
        std::memcpy(Out, ((FVMeshPart*)Handle)->Ref->Extra2C, 3 * sizeof(float));
        return 1;
    }

    // LAB_06c2d850: XOR AL,AL; RET 0x20.
    // formerly Slot15
    int DACOM_API SplitByPlane(void* /*Handle*/, void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/,
                         void* /*E*/, void* /*F*/) override
    {
        return 0;
    }

    // LAB_06c2da10 (RET 0x1C) -> AL: draw the part bound by Slot17 (the Handle argument is
    // not read) with the flag word translated: 0x4000 clear -> 0x40, 0x1000 -> 2, 0x8000 -> 0x80.
    // formerly Slot16
    int DACOM_API RenderInstance(ICamera* Camera, void* /*Handle*/, void* Inst, unsigned long Flags,
                         void* Matrix, void* Extra) override
    {
        unsigned long PartFlags = 0;
        if ((Flags & 0x4000) == 0)
            PartFlags = 0x40;
        if ((Flags & 0x1000) != 0)
            PartFlags |= 2;
        if ((Flags & 0x8000) != 0)
            PartFlags |= 0x80;
        return Bound->Draw(Camera, Inst, PartFlags, Matrix, (IMaterial*)Extra) ? 1 : 0;
    }

    // LAB_06c2dac0 (RET 0xC): invalid handle -> -1, else bind it.
    // formerly Slot17
    int DACOM_API BindMeshInstance(void* /*Inst*/, void* Handle) override
    {
        if (!VMeshHandleValid(Handle))
            return -1;
        Bound = (FVMeshPart*)Handle;
        return 0;
    }
};

// LAB_06c2db50 (factory vtable PTR_LAB_06c36414 slot 3, RET 0xC). Only a 0x10 AGGDESC whose
// name matches the factory's (else *Out = 0, -3); calloc failure -> *Out = 0, -4. FUN_06c2d720
// then requires AGGDESC +8 and +0xC to be set, otherwise the object is freed, *Out = 0, -2.
GENRESULT DACOM_API VMeshPluginFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    FRenderPluginDesc* Extended = (FRenderPluginDesc*)Desc;
    if (Extended->Size != 0x10 || std::strcmp(Extended->Name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    void* Memory = std::calloc(1, sizeof(FVMeshPlugin));
    if (Memory == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    FVMeshPlugin* Plugin = new (Memory) FVMeshPlugin();
    Plugin->Bound = nullptr;
    Plugin->RefCount = 1;
    GENRESULT Result = GR_OK;
    if (Extended->Parent == nullptr || Extended->Engine == nullptr)
        Result = (GENRESULT)0xFFFFFFFE;
    if (Result != GR_OK)
    {
        std::free(Plugin);
        *Out = nullptr;
        return Result;
    }
    *Out = Plugin;
    return Result;
}
