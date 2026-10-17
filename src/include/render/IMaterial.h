// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IRenderPipeline.h"

#include <cstddef>

struct FRpVertexSrcDesc;

// Shared material contract. Keep the declared calling conventions and slot order.
struct IMaterial : IDAComponent
{
    virtual GENRESULT DACOM_API Initialize(IDAComponent* Outer) = 0;
    virtual GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem) = 0;
    virtual GENRESULT DACOM_API SaveProperties(void* FileSystem) = 0;
    virtual GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) = 0;
    // Refresh texture handles; animated materials also advance by Delta.
    virtual GENRESULT DACOM_API ResolveTextures(float Delta) = 0;
    virtual GENRESULT DACOM_API Bind(void* Context) = 0;
    virtual GENRESULT DACOM_API Teardown(void* Context) = 0;
    virtual GENRESULT DACOM_API Submit(void* Context, unsigned long PrimType,
                                       unsigned long Start, unsigned long Count,
                                       unsigned long IbStart, unsigned long IndexSpan,
                                       unsigned long Flags) = 0;
    virtual GENRESULT DACOM_API SetName(const char* Name) = 0;
    virtual GENRESULT DACOM_API GetName(char* Out, unsigned long Size) = 0;
    virtual GENRESULT DACOM_API CopyClassName(char* Out, unsigned long Size) = 0;
    virtual GENRESULT DACOM_API GetPassCount(int* Out) = 0;
    virtual GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) = 0;
    virtual GENRESULT DACOM_API GetParameterCount(unsigned long* OutCount) = 0;
    // OutDesc receives one kMaterialParamDescSize-byte property descriptor.
    virtual GENRESULT DACOM_API GetParameterDescriptor(unsigned long Index, void* OutDesc) = 0;
    virtual GENRESULT DACOM_API GetParameterValue(unsigned long Index, void* InOutDesc) = 0;
    virtual GENRESULT DACOM_API SetParameterValue(unsigned long Index, void* Value) = 0;
    virtual const char* __thiscall ClassName() = 0;
    virtual void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) = 0;
    virtual GENRESULT __thiscall InitializeFromDescriptor(void* Arg) = 0;
    virtual GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) = 0;
    virtual GENRESULT __thiscall Setup() = 0;
    virtual bool __thiscall ValueWithinDeclaredRange(unsigned long Index, void* Entry,
                                                     void* Value) = 0;
    virtual GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry,
                                                     void* Value) = 0;
    // Return the count; OutBase is optional and receives the shared contiguous table.
    virtual unsigned long __thiscall GetParameterTable(void** OutBase) = 0;
    // Return the live backing address of the indexed property.
    virtual void* __thiscall GetParameterAddress(unsigned long Index) = 0;
    virtual GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) = 0;
    virtual unsigned long DACOM_API InnerAddRef() = 0;
    virtual unsigned long DACOM_API InnerRelease() = 0;
};
static const unsigned long kMaterialParamDescSize = 0x4C;
// Descriptor shared by material factories and their controlling outer component.
struct FMaterialAggregateDesc
{
    unsigned long Size;
    const char* NameAt4;
    IDAComponent* OuterAt8;
    void** InnerOutAtC;
    const char* ExtraAt10;
};
// Material properties retain their contiguous 17-float storage.
static_assert(sizeof(D3DMATERIAL8) == 17 * sizeof(float), "material render block size");

inline GENRESULT SetMaterialBlock(IRenderPipeline* Pipeline, const float* Block)
{
    return Pipeline->SetMaterial((const D3DMATERIAL8*)Block);
}

inline GENRESULT GetMaterialBlock(IRenderPipeline* Pipeline, float* Block)
{
    return Pipeline->GetMaterial((D3DMATERIAL8*)Block);
}

// Borrowed index-buffer handle and draw offset.
struct FIbScratch
{
    void* IndexBuffer;          // IRPIndexBuffer handle
    unsigned long Bias;         // RendComp: VMeshData+0x1C + VMeshRef.IndexStart;
};

// Borrowed context for Bind, Teardown and Submit.
struct FMaterialSubmitContext
{
    void* Inst;
    void* World;
    void* View;
    const FRpVertexSrcDesc* SourceVertices; // formerly Field0C
    unsigned short* Indices;    // deformable2 0
    FVbListing* VbListing;
    FIbScratch* IbScratch;
    IMaterial* ReplacedMaterial; // Original facegroup material while an override draws.
    void* Field20; // Material-specific context; meaning remains unknown.
};

static_assert(offsetof(FVbListing, BaseVertex) == 8, "listing+8 base vertex");
static_assert(sizeof(FVbListing) == 0xC, "VB listing 12 bytes");
static_assert(offsetof(FIbScratch, Bias) == 4, "scratch+4 IRPDraw bias");
static_assert(sizeof(FIbScratch) == 8, "IB scratch 8 bytes");
static_assert(offsetof(FMaterialSubmitContext, SourceVertices) == 0x0C, "ctx+0x0C source vertices");
static_assert(offsetof(FMaterialSubmitContext, Indices) == 0x10, "ctx+0x10 indices");
static_assert(offsetof(FMaterialSubmitContext, VbListing) == 0x14, "ctx+0x14 listing");
static_assert(offsetof(FMaterialSubmitContext, IbScratch) == 0x18, "ctx+0x18 IB scratch");
static_assert(offsetof(FMaterialSubmitContext, ReplacedMaterial) == 0x1C, "ctx+0x1C material");
static_assert(sizeof(FMaterialSubmitContext) == 0x24, "submit context 0x24");
