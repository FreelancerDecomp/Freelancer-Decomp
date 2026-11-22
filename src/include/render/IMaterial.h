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
    virtual GENRESULT DACOM_API Submit(void* Context, FlULong PrimType,
                                       FlULong Start, FlULong Count,
                                       FlULong IbStart, FlULong IndexSpan,
                                       FlULong Flags) = 0;
    virtual GENRESULT DACOM_API SetName(const char* Name) = 0;
    virtual GENRESULT DACOM_API GetName(char* Out, FlULong Size) = 0;
    virtual GENRESULT DACOM_API CopyClassName(char* Out, FlULong Size) = 0;
    virtual GENRESULT DACOM_API GetPassCount(int* Out) = 0;
    virtual GENRESULT DACOM_API GetRenderFlags(FlULong* Out) = 0;
    virtual GENRESULT DACOM_API GetParameterCount(FlULong* OutCount) = 0;
    // OutDesc receives one kMaterialParamDescSize-byte property descriptor.
    virtual GENRESULT DACOM_API GetParameterDescriptor(FlULong Index, void* OutDesc) = 0;
    virtual GENRESULT DACOM_API GetParameterValue(FlULong Index, void* InOutDesc) = 0;
    virtual GENRESULT DACOM_API SetParameterValue(FlULong Index, void* Value) = 0;
    virtual const char* __thiscall ClassName() = 0;
    virtual void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) = 0;
    virtual GENRESULT __thiscall InitializeFromDescriptor(void* Arg) = 0;
    virtual GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) = 0;
    virtual GENRESULT __thiscall Setup() = 0;
    virtual bool __thiscall ValueWithinDeclaredRange(FlULong Index, void* Entry,
                                                     void* Value) = 0;
    virtual GENRESULT __thiscall StoreParameterValue(FlULong Index, void* Entry,
                                                     void* Value) = 0;
    // Return the count; OutBase is optional and receives the shared contiguous table.
    virtual FlULong __thiscall GetParameterTable(void** OutBase) = 0;
    // Return the live backing address of the indexed property.
    virtual void* __thiscall GetParameterAddress(FlULong Index) = 0;
    virtual GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) = 0;
    virtual FlULong DACOM_API InnerAddRef() = 0;
    virtual FlULong DACOM_API InnerRelease() = 0;
};
static constexpr FlULong kMaterialParamDescSize = 0x4C;
// Material factories take dacom.h's FAGGDESC14 (formerly FMaterialAggregateDesc here).
// Material properties retain their contiguous 17-float storage.
FL_LAYOUT_ASSERT(sizeof(D3DMATERIAL8) == 17 * sizeof(float), "material render block size");

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
    FlULong Bias;         // RendComp: VMeshData+0x1C + VMeshRef.IndexStart;
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

FL_LAYOUT_ASSERT(offsetof(FVbListing, BaseVertex) == 8, "listing+8 base vertex");
FL_LAYOUT_ASSERT(sizeof(FVbListing) == 0xC, "VB listing 12 bytes");
FL_LAYOUT_ASSERT(offsetof(FIbScratch, Bias) == 4, "scratch+4 IRPDraw bias");
FL_LAYOUT_ASSERT(sizeof(FIbScratch) == 8, "IB scratch 8 bytes");
FL_LAYOUT_ASSERT(offsetof(FMaterialSubmitContext, SourceVertices) == 0x0C, "ctx+0x0C source vertices");
FL_LAYOUT_ASSERT(offsetof(FMaterialSubmitContext, Indices) == 0x10, "ctx+0x10 indices");
FL_LAYOUT_ASSERT(offsetof(FMaterialSubmitContext, VbListing) == 0x14, "ctx+0x14 listing");
FL_LAYOUT_ASSERT(offsetof(FMaterialSubmitContext, IbScratch) == 0x18, "ctx+0x18 IB scratch");
FL_LAYOUT_ASSERT(offsetof(FMaterialSubmitContext, ReplacedMaterial) == 0x1C, "ctx+0x1C material");
FL_LAYOUT_ASSERT(sizeof(FMaterialSubmitContext) == 0x24, "submit context 0x24");
