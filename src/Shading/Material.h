// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ShadingInternal.h"

class FMaterial;

// Property accessors return records from these shared contiguous schemas.
FShadingPropertyRecord* GetBtDetailPropertySchema();
FShadingPropertyRecord* GetDetailMapPropertySchema();
FShadingPropertyRecord* GetAtmospherePropertySchema();
FShadingPropertyRecord* GetSinglePassPropertySchema();
FShadingPropertyRecord* GetIllumDetailPropertySchema();
FShadingPropertyRecord* GetEcEtPropertySchema();
FShadingPropertyRecord* GetDcDtEtPropertySchema();

class FMaterialInner final : public IDAComponent
{
public:
    FMaterialInner();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    FMaterial* OwnerFromThis();
};

class FMaterial final : public IMaterial
{
public:
    FMaterial(const char* FactoryName, const char* ClassName, EMaterialBind BindKind,
              EMaterialSubmit SubmitKind);
    ~FMaterial() noexcept(false);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem) override;
    GENRESULT DACOM_API SaveProperties(void* FileSystem) override;
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTextures(float Delta) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long Zero, unsigned long Field,
                               unsigned long Flags) override;
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;
    GENRESULT DACOM_API GetParameterCount(unsigned long* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(unsigned long Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(unsigned long Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(unsigned long Index, void* Value) override;
    const char* __thiscall ClassName() override;
    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;
    bool __thiscall ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;
    void* __thiscall GetParameterAddress(unsigned long Index) override;
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API InnerAddRef() override;
    unsigned long DACOM_API InnerRelease() override;
    void ConfigureAggregation(FMaterialAggregateDesc* Desc);
    IRenderPipeline* Pipeline; // +4
    ITextureLibrary2* TexLib;    // Borrowed from the outer component.
    IVertexBufferManager* VBMgr;
    IRPDraw* RPDraw;
    const char* InstanceName;    // +14  LAB_06ebd810 GetCRC32 → IMatAnimLibrary slot 5
    FTexBlock* TexBlocks;        // +18
    unsigned long TexCount;      // +1C
    float DiffuseR;              // +20
    float DiffuseG;
    float DiffuseB;
    float Opacity;               // +2C
    float Ambient[4];            // +30
    float Specular[4];           // +40
    float Emissive[4];           // +50
    float SpecularPower;         // +60
    unsigned char bAlphablend;   // +64  "Oc"
    unsigned char bAlphatest;    // +65
    unsigned char Pad66[2];
    IImaInstance* MatAnim;               // +68  DcDt IMAInstance; Atmosphere retail = FadeOff float
    unsigned long Pad6C;         // +6C  Atmosphere retail = Alpha float
    FMaterialInner Inner;        // +70  SinglePass inner QI; Atmosphere retail = Scale float
    unsigned char bAtmosphereReady; // +74  byte FUN_06eb2b10 / FUN_06eb2b40 / LAB_06eb2d40
    unsigned char Pad75[3];
    float FadeOff;               // Atmosphere property 6, default 1.0 (DAT_06ed11c4)
    float Alpha;                 // Atmosphere property 7, default 0.75 (DAT_06ed11c8)
    float Scale;                 // Atmosphere property 8, default DAT_06ed11cc ≈ 1.025
    unsigned long DetailFlipU;
    unsigned long DetailFlipV;
    float DetailTileRate0;
    float DetailTileRate1;
    unsigned long DetailPassCount; // Detail +0x74, illum/masked +0x78: 1 or 2 passes
    bool bDetailSecondStageVerified; // Detail +0x79, illum/masked +0x7D
    bool bDetailMultiPassEnabled; // Detail +0x78, illum/masked +0x7C: global record0 gate


    unsigned long RefCount;
    const char* FactoryName;
    const char* ClassNameText;  // slot 20 answers with this, else FactoryName
    EMaterialBind BindKind;
    EMaterialSubmit SubmitKind;
    IDAComponent* Outer;
    bool bSecondaryTextureEnabled;
    bool bSecondaryTextureVerified;
    unsigned long TextureAllocationCount;


    void SetInstanceName(const char* Name);
    GENRESULT InitializeOwnedSchema();
    GENRESULT LoadDeclaredSchema(IFileSystem* FileSystem);
    GENRESULT ApplyVerifyMatAnim();
    void ApplyOcFlag();
    void BindDcDtTables(void* Context);
    void BindLightMapTables(void* Context);
    void BindNebulaTables(void* Context);
    void BindAtmosphereTables();
    void BindDcDtEtTables(void* Context);
    void BindEcEtTables(void* Context);
    void BindDetailMapTables(void* Context);
    GENRESULT ApplyVerifyDetailFamily();
    GENRESULT ApplyVerifyIllumDetail();
    bool ApplyDetailPassState(unsigned long Pass);
    bool ApplyIllumDetailPassState(unsigned long Pass);
    bool ApplyMaskedDetailPassState(unsigned long Pass);
    void BindBtDetailTables(void* Context);
    GENRESULT SubmitIndexedListing(FMaterialSubmitContext* Context, unsigned long PrimType,
                                   unsigned long Start, unsigned long Count, unsigned long IbStart,
                                   unsigned long IndexSpan);
    GENRESULT SubmitCopiedVertices(FMaterialSubmitContext* Context, unsigned long PrimType,
                                   unsigned long Start, unsigned long Count, unsigned long IbStart,
                                   unsigned long IndexSpan);
    GENRESULT SubmitAlphaTest(void* Context, unsigned long PrimType, unsigned long Start,
                              unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                              unsigned long Flags);
    GENRESULT SubmitKernel(void* Context, unsigned long PrimType, unsigned long Start, unsigned long Count,
                           unsigned long Zero, unsigned long Field, unsigned long Flags);
    GENRESULT SubmitDcDtTwoSecondPass(void* Context, unsigned long PrimType, unsigned long Start,
                                 unsigned long Count, unsigned long Zero, unsigned long Field);
    GENRESULT SubmitBtDetailTwoSecondPass(void* Context, unsigned long PrimType,
                                          unsigned long Start, unsigned long Count,
                                          unsigned long IbStart, unsigned long IndexSpan);
    GENRESULT SubmitAtmosphereLock(void* Context, unsigned long PrimType, unsigned long Start,
                                   unsigned long Count, unsigned long Zero, unsigned long Field);
};

class FBatchedFace final : public IBatchedMaterial
{
public:
    FBatchedFace();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInnerMaterial(void* Name) override;
    GENRESULT DACOM_API SetBatchBin(void* Value) override;
    GENRESULT DACOM_API GetBatchBin(void** Out) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, unsigned long PrimType, unsigned long Start, unsigned long Count,
                              unsigned long Zero, unsigned long Field, unsigned long Flags) override;

    class FBatchedMaterial* OwnerFromThis();
};

class FBatchedController final : public IDAComponent
{
public:
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    class FBatchedMaterial* OwnerFromThis();
};

class FBatchedMaterial final : public IMaterial
{
public:
    FBatchedMaterial();
    ~FBatchedMaterial() noexcept(false);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem) override;
    GENRESULT DACOM_API SaveProperties(void* FileSystem) override;
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTextures(float Delta) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long Zero, unsigned long Field,
                               unsigned long Flags) override;
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;
    GENRESULT DACOM_API GetParameterCount(unsigned long* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(unsigned long Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(unsigned long Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(unsigned long Index, void* Value) override;
    const char* __thiscall ClassName() override;
    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;
    bool __thiscall ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;
    void* __thiscall GetParameterAddress(unsigned long Index) override;
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API InnerAddRef() override;
    unsigned long DACOM_API InnerRelease() override;
    void ConfigureAggregation(FMaterialAggregateDesc* Desc);
    GENRESULT InitializeFromDescriptor(FMaterialAggregateDesc* Desc);
    FBatchedFace Batched; // +4  QI 1.11_IBatchedMaterial (vptr only)
    unsigned long Field8; // +8  LAB_06eb4480 / FUN_06eb40f0 default -1
    IDAComponent* InnerController; // +C retained nondelegating factory controller
    IMaterial* Inner;     // +10 LAB_06eb4380
    void* Batcher;        // +14 Initialize QI IMaterialBatcher
    unsigned long RefCount;
    IDAComponent* Outer;

    FBatchedController Controller;
    IMaterial* InnerMaterial() const;
};
