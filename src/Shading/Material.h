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
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    FMaterial* OwnerFromThis();
};

class FMaterial final : public IMaterial
{
public:
    FMaterial(const char* FactoryName, const char* ClassName, EMaterialBind BindKind,
              EMaterialSubmit SubmitKind);
    ~FMaterial() noexcept(false);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem) override;
    GENRESULT DACOM_API SaveProperties(void* FileSystem) override;
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTextures(float Delta) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, FlULong PrimType, FlULong Start,
                               FlULong Count, FlULong Zero, FlULong Field,
                               FlULong Flags) override;
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, FlULong Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, FlULong Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(FlULong* Out) override;
    GENRESULT DACOM_API GetParameterCount(FlULong* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(FlULong Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(FlULong Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(FlULong Index, void* Value) override;
    const char* __thiscall ClassName() override;
    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;
    bool __thiscall ValueWithinDeclaredRange(FlULong Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(FlULong Index, void* Entry, void* Value) override;
    FlULong __thiscall GetParameterTable(void** OutBase) override;
    void* __thiscall GetParameterAddress(FlULong Index) override;
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API InnerAddRef() override;
    FlULong DACOM_API InnerRelease() override;
    void ConfigureAggregation(FAGGDESC14* Desc);
    IRenderPipeline* Pipeline; // +4
    ITextureLibrary2* TexLib;    // Borrowed from the outer component.
    IVertexBufferManager* VBMgr;
    IRPDraw* RPDraw;
    const char* InstanceName;    // +14  LAB_06ebd810 GetCRC32 → IMatAnimLibrary slot 5
    FTexBlock* TexBlocks;        // +18
    FlULong TexCount;      // +1C
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
    FlULong Pad6C;         // +6C  Atmosphere retail = Alpha float
    FMaterialInner Inner;        // +70  SinglePass inner QI; Atmosphere retail = Scale float
    unsigned char bAtmosphereReady; // +74  byte FUN_06eb2b10 / FUN_06eb2b40 / LAB_06eb2d40
    unsigned char Pad75[3];
    float FadeOff;               // Atmosphere property 6, default 1.0 (DAT_06ed11c4)
    float Alpha;                 // Atmosphere property 7, default 0.75 (DAT_06ed11c8)
    float Scale;                 // Atmosphere property 8, default DAT_06ed11cc ≈ 1.025
    FlULong DetailFlipU;
    FlULong DetailFlipV;
    float DetailTileRate0;
    float DetailTileRate1;
    FlULong DetailPassCount; // Detail +0x74, illum/masked +0x78: 1 or 2 passes
    bool bDetailSecondStageVerified; // Detail +0x79, illum/masked +0x7D
    bool bDetailMultiPassEnabled; // Detail +0x78, illum/masked +0x7C: global record0 gate


    FlULong RefCount;
    const char* FactoryName;
    const char* ClassNameText;  // slot 20 answers with this, else FactoryName
    EMaterialBind BindKind;
    EMaterialSubmit SubmitKind;
    IDAComponent* Outer;
    bool bSecondaryTextureEnabled;
    bool bSecondaryTextureVerified;
    FlULong TextureAllocationCount;


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
    bool ApplyDetailPassState(FlULong Pass);
    bool ApplyIllumDetailPassState(FlULong Pass);
    bool ApplyMaskedDetailPassState(FlULong Pass);
    void BindBtDetailTables(void* Context);
    GENRESULT SubmitIndexedListing(FMaterialSubmitContext* Context, FlULong PrimType,
                                   FlULong Start, FlULong Count, FlULong IbStart,
                                   FlULong IndexSpan);
    GENRESULT SubmitCopiedVertices(FMaterialSubmitContext* Context, FlULong PrimType,
                                   FlULong Start, FlULong Count, FlULong IbStart,
                                   FlULong IndexSpan);
    GENRESULT SubmitAlphaTest(void* Context, FlULong PrimType, FlULong Start,
                              FlULong Count, FlULong IbStart, FlULong IndexSpan,
                              FlULong Flags);
    GENRESULT SubmitKernel(void* Context, FlULong PrimType, FlULong Start, FlULong Count,
                           FlULong Zero, FlULong Field, FlULong Flags);
    GENRESULT SubmitDcDtTwoSecondPass(void* Context, FlULong PrimType, FlULong Start,
                                 FlULong Count, FlULong Zero, FlULong Field);
    GENRESULT SubmitBtDetailTwoSecondPass(void* Context, FlULong PrimType,
                                          FlULong Start, FlULong Count,
                                          FlULong IbStart, FlULong IndexSpan);
    GENRESULT SubmitAtmosphereLock(void* Context, FlULong PrimType, FlULong Start,
                                   FlULong Count, FlULong Zero, FlULong Field);
};

class FBatchedFace final : public IBatchedMaterial
{
public:
    FBatchedFace();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    GENRESULT DACOM_API CreateInnerMaterial(void* Name) override;
    GENRESULT DACOM_API SetBatchBin(void* Value) override;
    GENRESULT DACOM_API GetBatchBin(void** Out) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, FlULong PrimType, FlULong Start, FlULong Count,
                              FlULong Zero, FlULong Field, FlULong Flags) override;

    class FBatchedMaterial* OwnerFromThis();
};

class FBatchedController final : public IDAComponent
{
public:
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    class FBatchedMaterial* OwnerFromThis();
};

class FBatchedMaterial final : public IMaterial
{
public:
    FBatchedMaterial();
    ~FBatchedMaterial() noexcept(false);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem) override;
    GENRESULT DACOM_API SaveProperties(void* FileSystem) override;
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTextures(float Delta) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, FlULong PrimType, FlULong Start,
                               FlULong Count, FlULong Zero, FlULong Field,
                               FlULong Flags) override;
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, FlULong Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, FlULong Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(FlULong* Out) override;
    GENRESULT DACOM_API GetParameterCount(FlULong* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(FlULong Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(FlULong Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(FlULong Index, void* Value) override;
    const char* __thiscall ClassName() override;
    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;
    bool __thiscall ValueWithinDeclaredRange(FlULong Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(FlULong Index, void* Entry, void* Value) override;
    FlULong __thiscall GetParameterTable(void** OutBase) override;
    void* __thiscall GetParameterAddress(FlULong Index) override;
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API InnerAddRef() override;
    FlULong DACOM_API InnerRelease() override;
    void ConfigureAggregation(FAGGDESC14* Desc);
    GENRESULT InitializeFromDescriptor(FAGGDESC14* Desc);
    FBatchedFace Batched; // +4  QI 1.11_IBatchedMaterial (vptr only)
    FlULong Field8; // +8  LAB_06eb4480 / FUN_06eb40f0 default -1
    IDAComponent* InnerController; // +C retained nondelegating factory controller
    IMaterial* Inner;     // +10 LAB_06eb4380
    void* Batcher;        // +14 Initialize QI IMaterialBatcher
    FlULong RefCount;
    IDAComponent* Outer;

    FBatchedController Controller;
    IMaterial* InnerMaterial() const;
};
