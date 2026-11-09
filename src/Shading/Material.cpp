// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_float_load.h"
#include "Material.h"
#include "MaterialAnimation.h"
#include "MaterialLibrary.h"
#include "engine/engbase.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

// Retail DAT_06ed188c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed188c[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x0, 0x0, 0x0};
// Retail DAT_06ed18ac, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed18ac[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x1, 0x13, 0x5, 0x14, 0x6, 0x0, 0x0};
// Retail DAT_06ed18dc, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed18dc[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
// Retail DAT_06ed1914, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1914[] = {0x1, 0x5, 0x2, 0x2, 0x3, 0x1, 0x4, 0x4, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
// Retail DAT_06ed194c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed194c[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
// Retail DAT_06ed1e7c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1e7c[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x0, 0x0, 0x0};
// Retail DAT_06ed1e9c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1e9c[] = {0x89, 0x0, 0x1D, 0x0, 0x1B, 0x1, 0x13, 0x5, 0x14, 0x6, 0x0, 0x0};
// Retail DAT_06ed1ecc, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1ecc[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
// Retail DAT_06ed1f04, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1f04[] = {0x1, 0x5, 0x2, 0x2, 0x3, 0x1, 0x4, 0x4, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
// Retail DAT_06ed1f3c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1f3c[] = {0x1, 0x2, 0x2, 0x2, 0x3, 0x0, 0x4, 0x2, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
// Retail DAT_06ed1f74, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed1f74[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
// Retail DAT_06ed220c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed220c[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x0, 0x0, 0x0};
// Retail DAT_06ed222c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed222c[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x1, 0x13, 0x5, 0x14, 0x6, 0x0, 0x0};
// Retail DAT_06ed225c, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed225c[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
// Retail DAT_06ed2294, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed2294[] = {0x1, 0x5, 0x2, 0x2, 0x3, 0x1, 0x4, 0x4, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
// Retail DAT_06ed22cc, bytes read from EXE/Shading.dll.
static const unsigned long GDetailTable_06ed22cc[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};

// LAB_06eb81e0: all four tables decoded from their own retail PE bytes.
static const unsigned long GDetailMapTable_06ed1a9c[] = {0x89, 1, 0x1D, 0, 0x1B, 0, 0, 0};
static const unsigned long GDetailMapTable_06ed1abc[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0, 0};
static const unsigned long GDetailMapTable_06ed1af4[] = {1, 5, 2, 2, 3, 1, 4, 4, 5, 2, 6, 1, 0, 0};
static const unsigned long GDetailMapTable_06ed1b2c[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0, 0};

// Own retail PE state tables for material Bind callbacks.
static const unsigned long GBindTable_06ed209c[] = {0x8D, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed20ac[] = {0x1, 0x2, 0x2, 0x2, 0x3, 0x0, 0x4, 0x2, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed20e4[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
static const unsigned long GBindTable_06ed206c[] = {0x89, 0x0, 0x1D, 0x0, 0x1B, 0x1, 0x13, 0x9, 0x14, 0x1, 0x0, 0x0};

// Own retail PE state tables for material Bind callbacks.
static const unsigned long GBindTable_06ed26dc[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed2714[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
static const unsigned long GBindTable_06ed26ac[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x1, 0x13, 0x5, 0x14, 0x6, 0x0, 0x0};
static const unsigned long GBindTable_06ed268c[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x0, 0x0, 0x0};

// Own retail PE state tables for material Bind callbacks.
static const unsigned long GBindTable_06ed1484[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed14a4[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed14dc[] = {0x1, 0x5, 0x2, 0x2, 0x3, 0x1, 0x4, 0x4, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
static const unsigned long GBindTable_06ed1514[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};

// Own retail PE state tables for material Bind callbacks.
static const unsigned long GBindTable_06ed16cc[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed16ec[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed1724[] = {0x1, 0x7, 0x2, 0x2, 0x3, 0x1, 0x4, 0x4, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
static const unsigned long GBindTable_06ed175c[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};

static const unsigned long GBindTable_06ed1c84[] = {0x1, 0x4, 0x2, 0x2, 0x3, 0x0, 0x4, 0x4, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0};
static const unsigned long GBindTable_06ed1cbc[] = {0x1, 0x1, 0x2, 0x2, 0x3, 0x1, 0x4, 0x1, 0x5, 0x2, 0x6, 0x1, 0x0, 0x0};
static const unsigned long GBindTable_06ed1c54[] = {0x89, 0x1, 0x1D, 0x0, 0x1B, 0x1, 0x13, 0x2, 0x14, 0x2, 0x0, 0x0};

// FUN_06eb7120: one texture block, four calls in the original order.
static void BindDetailTexture(IRenderPipeline* Pipeline, const FTexBlock& Block,
                              unsigned long Stage)
{
    Pipeline->SetTextureStageTexture((int)Stage,
                                    (void*)(Block.Handle));
    Pipeline->SetTextureStageState((int)Stage, D3DTSS_TEXCOORDINDEX,
                                  Block.Flags >= 4 ? Block.Flags - 4 : 0);
    Pipeline->SetTextureStageState((int)Stage, D3DTSS_ADDRESSU, Block.AddressU + 1);
    Pipeline->SetTextureStageState((int)Stage, D3DTSS_ADDRESSV, Block.AddressV + 1);
}

static void ApplyDetailTable(IRenderPipeline* Pipeline, const unsigned long* Table,
                             bool bTextureStage, unsigned long Stage)
{
    for (unsigned long Index = 0; Table[Index] != 0; Index += 2)
    {
        if (bTextureStage)
            Pipeline->SetTextureStageState((int)Stage, Table[Index], Table[Index + 1]);
        else
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)Table[Index], Table[Index + 1]);
    }
}

static void BindMaterialAndContext(IRenderPipeline* Pipeline, const float* Material,
                                   void* Context)
{
    SetMaterialBlock(Pipeline, Material);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            Pipeline->SetRenderState(D3DRS_COLORVERTEX, 1);
    }
}

static void SetDetailTileTransform(IRenderPipeline* Pipeline, float TileRate)
{
    // Only elements 0 and 5 are written after the sixteen-dword zero fill.
    float Matrix[16] = {};
    Matrix[0] = TileRate;
    Matrix[5] = TileRate;
    Pipeline->SetTextureStageTransform(1, Matrix);
}

bool FMaterial::ApplyDetailPassState(unsigned long Pass)
{
    // FUN_06eb71f0: +0x74 is the pass count, +0x79 is stage-1 verification.
    if (DetailPassCount == 1)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed18dc, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed188c, false, 0);
        return true;
    }
    if (Pass == 0)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed18dc, true, 0);
        unsigned long DisabledStage = 1;
        if (bDetailSecondStageVerified)
        {
            BindDetailTexture(Pipeline, TexBlocks[1], 1);
            ApplyDetailTable(Pipeline, GDetailTable_06ed1914, true, 1);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
            SetDetailTileTransform(Pipeline, DetailTileRate0);
            DisabledStage = 2;
        }
        ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, DisabledStage);
        ApplyDetailTable(Pipeline, GDetailTable_06ed188c, false, 0);
    }
    else if (Pass == 1 && bDetailSecondStageVerified)
    {
        ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed18ac, false, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    }
    return true;
}

bool FMaterial::ApplyIllumDetailPassState(unsigned long Pass)
{
    // FUN_06eb9b10: no pass-count test; pass 1 always uses the third block.
    if (Pass == 0)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1ecc, true, 0);
        unsigned long DisabledStage = 1;
        if (bDetailSecondStageVerified)
        {
            BindDetailTexture(Pipeline, TexBlocks[1], 1);
            ApplyDetailTable(Pipeline, GDetailTable_06ed1f04, true, 1);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
            SetDetailTileTransform(Pipeline, DetailTileRate0);
            DisabledStage = 2;
        }
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f74, true, DisabledStage);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1e7c, false, 0);
    }
    else if (Pass == 1)
    {
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f3c, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f04, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f74, true, 2);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1e9c, false, 0);
        BindDetailTexture(Pipeline, TexBlocks[2], 1);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
        SetDetailTileTransform(Pipeline, DetailTileRate1);
    }
    return true;
}

bool FMaterial::ApplyMaskedDetailPassState(unsigned long Pass)
{
    // FUN_06ebb890: mode 1 ignores the requested pass.
    if (DetailPassCount == 1)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed225c, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed220c, false, 0);
        return true;
    }
    if (Pass == 0)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed225c, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed2294, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 2);
        ApplyDetailTable(Pipeline, GDetailTable_06ed220c, false, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
        SetDetailTileTransform(Pipeline, DetailTileRate0);
    }
    else if (Pass == 1)
    {
        ApplyDetailTable(Pipeline, GDetailTable_06ed2294, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 2);
        ApplyDetailTable(Pipeline, GDetailTable_06ed222c, false, 0);
        BindDetailTexture(Pipeline, TexBlocks[2], 1);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
        SetDetailTileTransform(Pipeline, DetailTileRate1);
    }
    return true;
}

// Raw retail tables at 06ed1140/1178 and 06ed23fc/2434 are identical.
static const unsigned long GAtmosphereNebulaTss0[] = {
    1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0, 0};
static const unsigned long GAtmosphereNebulaTss1[] = {
    1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0, 0};
// Raw retail RS tables 06ed1100 and 06ed23cc.
static const unsigned long GAtmosphereRs[] = {0x89, 1, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0, 0};
static const unsigned long GNebulaRs[] = {0x89, 0, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 2, 0, 0};

namespace
{
// Shared contiguous property schemas; lazy initialization publishes the guard first.
char GPropertyMetadata_06ed56c4[4] = {};
FShadingPropertyRecord GAtmospherePropertySchema[9] = {};
bool GbAtmospherePropertySchemaInitialized = false;

// Single-pass and detail families share their respective contiguous property tables.
FShadingPropertyRecord GSinglePassPropertySchema[10] = {};
bool GbSinglePassPropertySchemaInitialized = false;
FShadingPropertyRecord GIllumDetailPropertySchema[18] = {};
bool GbIllumDetailPropertySchemaInitialized = false;
FShadingPropertyRecord GMaskedDetailPropertySchema[18] = {};
bool GbMaskedDetailPropertySchemaInitialized = false;
FShadingPropertyRecord GBtDetailPropertySchema[13] = {};
bool GbBtDetailPropertySchemaInitialized = false;
FShadingPropertyRecord GDetailMapPropertySchema[13] = {};
bool GbDetailMapPropertySchemaInitialized = false;
FShadingPropertyRecord GDetail2PassPropertySchema[13] = {};
bool GbDetail2PassPropertySchemaInitialized = false;
FShadingPropertyRecord GEcEtPropertySchema[5] = {};
bool GbEcEtPropertySchemaInitialized = false;
FShadingPropertyRecord GDcDtEtPropertySchema[9] = {};
bool GbDcDtEtPropertySchemaInitialized = false;
FShadingPropertyRecord GNebulaPropertySchema[4] = {};
bool GbNebulaPropertySchemaInitialized = false;
FShadingPropertyRecord GLightMapPropertySchema[4] = {};
bool GbLightMapPropertySchemaInitialized = false;

bool IsSinglePassPropertySchemaFamily(const char* FactoryName)
{
    static const char* const Names[] = {"DcDt",       "DcDtEc",       "DcDtOcOt",
                                        "DcDtEcOcOt", "DcDtTwo",      "DcDtEcTwo",
                                        "DcDtOcOtTwo", "DcDtEcOcOtTwo"};
    if (FactoryName == nullptr)
        return false;
    for (unsigned long Index = 0; Index < sizeof(Names) / sizeof(Names[0]); ++Index)
    {
        if (std::strcmp(FactoryName, Names[Index]) == 0)
            return true;
    }
    return false;
}

// FUN_06eb2a60 clears only metadata, tags and first payload words.
FShadingPropertyRecord* ConstructPropertyRecord(FShadingPropertyRecord* Record)
{
    Record->Current.Type = 0;
    Record->Current.Payload0 = 0;
    Record->Default.Type = 0;
    Record->Default.Payload0 = 0;
    Record->Saved.Type = 0;
    Record->Saved.Payload0 = 0;
    Record->Flags = 0;
    Record->Name = nullptr;
    Record->Description = nullptr;
    Record->Callback = nullptr;
    return Record;
}

void ConstructPropertyRecords(FShadingPropertyRecord* Records, unsigned long Count)
{
    for (unsigned long Index = 0; Index < Count; ++Index)
        ConstructPropertyRecord(&Records[Index]);
}

// All schema on-exit bodies clear the COM pointer before calling Release.
void ReleaseSchemaPayload(FShadingPropertyValue& Value)
{
    if (Value.Type == 6)
    {
        IDAComponent* Component = (IDAComponent*)(Value.Payload0);
        Value.Payload0 = 0;
        if (Component != nullptr)
            Component->Release();
        Value.Payload0 = 0;
    }
    else
        Value.ReleaseOwnedPayload();
}

void DestroyPropertyRecords(FShadingPropertyRecord* Records, unsigned long Count)
{
    for (unsigned long Remaining = Count; Remaining != 0; --Remaining)
    {
        FShadingPropertyRecord& Record = Records[Remaining - 1];
        Record.Flags = 0;
        Record.Name = nullptr;
        Record.Description = nullptr;
        Record.Callback = nullptr;
        ReleaseSchemaPayload(Record.Saved);
        ReleaseSchemaPayload(Record.Default);
        ReleaseSchemaPayload(Record.Current);
    }
}

void DestroyAtmospherePropertySchema()
{
    DestroyPropertyRecords(GAtmospherePropertySchema, 9);
}

void DestroySinglePassPropertySchema()
{
    DestroyPropertyRecords(GSinglePassPropertySchema, 10);
}

void DestroyIllumDetailPropertySchema()
{
    DestroyPropertyRecords(GIllumDetailPropertySchema, 18);
}

void DestroyEcEtPropertySchema()
{
    DestroyPropertyRecords(GEcEtPropertySchema, 5);
}

void DestroyDcDtEtPropertySchema()
{
    DestroyPropertyRecords(GDcDtEtPropertySchema, 9);
}

void SetAtmospherePropertyRecord(FShadingPropertyRecord& Record, unsigned long Flags,
                                 const char* Name, const char* Description,
                                 unsigned long CurrentType, const void* Current,
                                 unsigned long DefaultType, const void* Default,
                                 unsigned long SavedType, const void* Saved)
{
    Record.Flags = Flags;
    Record.Name = Name;
    Record.Description = Description;
    Record.Callback = GPropertyMetadata_06ed56c4;
    Record.Current.Assign(CurrentType, Current);
    Record.Default.Assign(DefaultType, Default);
    Record.Saved.Assign(SavedType, Saved);
}

void SetSinglePassPropertyRecord(FShadingPropertyRecord& Record, unsigned long Flags,
                                 const char* Name, const char* Description,
                                 unsigned long CurrentType, const void* Current,
                                 unsigned long DefaultType, const void* Default,
                                 unsigned long SavedType, const void* Saved)
{
    Record.Flags = Flags;
    Record.Name = Name;
    Record.Description = Description;
    // The retail instruction stores ESI itself (06ed56c4), not [ESI].
    Record.Callback = GPropertyMetadata_06ed56c4;
    Record.Current.Assign(CurrentType, Current);
    Record.Default.Assign(DefaultType, Default);
    Record.Saved.Assign(SavedType, Saved);
}

void InitializeAtmospherePropertySchema()
{
    if (!GbAtmospherePropertySchemaInitialized)
    {
        GbAtmospherePropertySchemaInitialized = true;
        ConstructPropertyRecords(GAtmospherePropertySchema, 9);
        std::atexit(DestroyAtmospherePropertySchema);
    }
    if (GAtmospherePropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[0], 0x0, "Ac", "Ambient Color",
                                3, Payload_0_0, 3, Payload_0_1, 3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x41200000, 0x41200000, 0x41200000};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[1], 0x0, "Dc", "Diffuse Color",
                                3, Payload_1_0, 3, Payload_1_1, 3, Payload_1_2);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[2], 0x0, "Dt_name", "Fade 1D Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[3], 0x0, "Dt_flags", "Fade 1D Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[4], 0x20000, "Dt_flags", "Fade 1D Texture U Address Mode",
                                9, (void*)2, 9, nullptr, 9, nullptr);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[5], 0x40000, "Dt_flags", "Fade 1D Texture V Address Mode",
                                9, (void*)2, 9, nullptr, 9, nullptr);
    static const unsigned long Payload_6_0[] = {0x3F800000};
    static const unsigned long Payload_6_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_6_2[] = {0x7F7FFFFF};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[6], 0x0, "Fade", "Fade Off",
                                2, Payload_6_0, 2, Payload_6_1, 2, Payload_6_2);
    static const unsigned long Payload_7_0[] = {0x3F400000};
    static const unsigned long Payload_7_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_7_2[] = {0x7F7FFFFF};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[7], 0x0, "Alpha", "Alpha",
                                2, Payload_7_0, 2, Payload_7_1, 2, Payload_7_2);
    static const unsigned long Payload_8_0[] = {0x3F833333};
    static const unsigned long Payload_8_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_8_2[] = {0x7F7FFFFF};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[8], 0x0, "Scale", "Scale",
                                2, Payload_8_0, 2, Payload_8_1, 2, Payload_8_2);
}

void InitializeSinglePassPropertySchema()
{
    if (!GbSinglePassPropertySchemaInitialized)
    {
        GbSinglePassPropertySchemaInitialized = true;
        ConstructPropertyRecords(GSinglePassPropertySchema, 10);
        std::atexit(DestroySinglePassPropertySchema);
    }
    if (GSinglePassPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[0], 0x0, "Ac", "Ambient Color",
                                3, Payload_0_0, 3, Payload_0_1, 3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[1], 0x0, "Dc", "Diffuse Color",
                                3, Payload_1_0, 3, Payload_1_1, 3, Payload_1_2);
    static const unsigned long Payload_2_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[2], 0x0, "Ec", "Emissive Color",
                                3, Payload_2_0, 3, Payload_2_1, 3, Payload_2_2);
    static const unsigned long Payload_3_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[3], 0x0, "Sc", "Specular Color",
                                3, Payload_3_0, 3, Payload_3_1, 3, Payload_3_2);
    static const unsigned long Payload_4_0[] = {0x00000000};
    static const unsigned long Payload_4_1[] = {0x00000000};
    static const unsigned long Payload_4_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[4], 0x0, "Sp", "Specular Power",
                                2, Payload_4_0, 2, Payload_4_1, 2, Payload_4_2);
    static const unsigned long Payload_5_0[] = {0x3F800000};
    static const unsigned long Payload_5_1[] = {0x00000000};
    static const unsigned long Payload_5_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[5], 0x0, "Oc", "Opacity ",
                                2, Payload_5_0, 2, Payload_5_1, 2, Payload_5_2);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[6], 0x0, "Dt_name", "Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[7], 0x0, "Dt_flags", "Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[8], 0x20000, "Dt_flags", "Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[9], 0x40000, "Dt_flags", "Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
}

void InitializeDetailPropertyRecords(FShadingPropertyRecord* Records)
{
    static const unsigned long kZero3[3] = {0, 0, 0};
    static const unsigned long kWhite3[3] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long kFourBits = 0x40800000;
    static const unsigned long kFloatMinBits = 0xFF7FFFFF;
    static const unsigned long kFloatMaxBits = 0x7F7FFFFF;

    SetSinglePassPropertyRecord(Records[0], 0, "Ac", "Ambient Color", 3,
                                kWhite3, 3, kZero3, 3, kWhite3);
    SetSinglePassPropertyRecord(Records[1], 0, "Dc", "Diffuse Color", 3,
                                kWhite3, 3, kZero3, 3, kWhite3);
    SetSinglePassPropertyRecord(Records[2], 0, "Dt_name",
                                "Diffuse Texture Name", 7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(Records[3], 0, "Dt_flags",
                                "Diffuse Texture Wrap Mode", 8, (void*)4,
                                8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(Records[4], 0x20000, "Dt_flags",
                                "Diffuse Texture U Address Mode", 9, (void*)2, 9, nullptr,
                                9, nullptr);
    SetSinglePassPropertyRecord(Records[5], 0x40000, "Dt_flags",
                                "Diffuse Texture V Address Mode", 9, (void*)2, 9, nullptr,
                                9, nullptr);
    SetSinglePassPropertyRecord(Records[6], 0, "flip u", "Flip U ", 1,
                                nullptr, 1, nullptr, 1, (void*)1);
    SetSinglePassPropertyRecord(Records[7], 0, "flip v", "Flip V ", 1,
                                nullptr, 1, nullptr, 1, (void*)1);
    SetSinglePassPropertyRecord(Records[8], 0, "Dm0_name",
                                "Detail Texture Alpha0 Name", 7, "(None)", 7, nullptr, 7,
                                nullptr);
    SetSinglePassPropertyRecord(Records[9], 0, "Dm0_flags",
                                "Detail Texture Alpha0 Wrap Mode", 8,
                                (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(Records[10], 0x20000, "Dm0_flags",
                                "Detail Texture Alpha0 U Address Mode", 9, nullptr, 9,
                                nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[11], 0x40000, "Dm0_flags",
                                "Detail Texture Alpha0 V Address Mode", 9, nullptr, 9,
                                nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[12], 0, "TileRate0",
                                "Tile Rate Alpha0", 2, &kFourBits, 2, &kFloatMinBits, 2,
                                &kFloatMaxBits);
    SetSinglePassPropertyRecord(Records[13], 0, "Dm1_name",
                                "Detail Texture Alpha1 Name", 7, "(None)", 7, nullptr, 7,
                                nullptr);
    SetSinglePassPropertyRecord(Records[14], 0, "Dm1_flags",
                                "Detail Texture Alpha1 Wrap Mode", 8,
                                (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(Records[15], 0x20000, "Dm1_flags",
                                "Detail Texture Alpha1 U Address Mode", 9, nullptr, 9,
                                nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[16], 0x40000, "Dm1_flags",
                                "Detail Texture Alpha1 V Address Mode", 9, nullptr, 9,
                                nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[17], 0, "TileRate1",
                                "Tile Rate Alpha1", 2, &kFourBits, 2, &kFloatMinBits, 2,
                                &kFloatMaxBits);
}

void InitializeIllumDetailPropertySchema()
{
    if (!GbIllumDetailPropertySchemaInitialized)
    {
        GbIllumDetailPropertySchemaInitialized = true;
        ConstructPropertyRecords(GIllumDetailPropertySchema, 18);
        std::atexit(DestroyIllumDetailPropertySchema);
    }
    // The second retail guard tests record zero Current.Type, independently.
    if (GIllumDetailPropertySchema[0].Current.Type != 0)
        return;
    InitializeDetailPropertyRecords(GIllumDetailPropertySchema);
}

void DestroyMaskedDetailPropertySchema()
{
    DestroyPropertyRecords(GMaskedDetailPropertySchema, 18);
}

void InitializeMaskedDetailPropertySchema()
{
    if (!GbMaskedDetailPropertySchemaInitialized)
    {
        GbMaskedDetailPropertySchemaInitialized = true;
        ConstructPropertyRecords(GMaskedDetailPropertySchema, 18);
        std::atexit(DestroyMaskedDetailPropertySchema);
    }
    // The second retail guard tests record zero Current.Type, independently.
    if (GMaskedDetailPropertySchema[0].Current.Type != 0)
        return;
    InitializeDetailPropertyRecords(GMaskedDetailPropertySchema);
}

void InitializeDetail2PassPropertyRecords(FShadingPropertyRecord* Records)
{
    static const unsigned long kZero3[3] = {0, 0, 0};
    static const unsigned long kWhite3[3] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long kTen3[3] = {0x41200000, 0x41200000, 0x41200000};
    static const unsigned long kFourBits = 0x40800000;
    static const unsigned long kFloatMinBits = 0xFF7FFFFF;
    static const unsigned long kFloatMaxBits = 0x7F7FFFFF;

    SetSinglePassPropertyRecord(Records[0], 0, "Ac", "Ambient Color", 3,
                                kWhite3, 3, kZero3, 3, kWhite3);
    SetSinglePassPropertyRecord(Records[1], 0, "Dc", "Diffuse Color", 3,
                                kWhite3, 3, kZero3, 3, kTen3);
    SetSinglePassPropertyRecord(Records[2], 0, "Dt_name",
                                "Diffuse Texture Name", 7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(Records[3], 0, "Dt_flags",
                                "Diffuse Texture Wrap Mode", 8, (void*)4,
                                8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(Records[4], 0x20000, "Dt_flags",
                                "Diffuse Texture U Address Mode", 9, (void*)2, 9, nullptr,
                                9, nullptr);
    SetSinglePassPropertyRecord(Records[5], 0x40000, "Dt_flags",
                                "Diffuse Texture V Address Mode", 9, (void*)2, 9, nullptr,
                                9, nullptr);
    SetSinglePassPropertyRecord(Records[6], 0, "flip u", "Flip U ", 1,
                                nullptr, 1, nullptr, 1, (void*)1);
    SetSinglePassPropertyRecord(Records[7], 0, "flip v", "Flip V ", 1,
                                nullptr, 1, nullptr, 1, (void*)1);
    SetSinglePassPropertyRecord(Records[8], 0, "Dm1_name",
                                "Detail Texture Name", 7, "(None)", 7, nullptr, 7,
                                nullptr);
    SetSinglePassPropertyRecord(Records[9], 0, "Dm1_flags",
                                "Detail Texture Wrap Mode", 8,
                                (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(Records[10], 0x20000, "Dm1_flags",
                                "Detail Texture U Address Mode", 9, nullptr, 9,
                                nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[11], 0x40000, "Dm1_flags",
                                "Detail Texture V Address Mode", 9, nullptr, 9,
                                nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[12], 0, "TileRate",
                                "Tile Rate ", 2, &kFourBits, 2, &kFloatMinBits, 2,
                                &kFloatMaxBits);
}

void DestroyDetail2PassPropertySchema()
{
    DestroyPropertyRecords(GDetail2PassPropertySchema, 13);
}

void InitializeDetail2PassPropertySchema()
{
    if (!GbDetail2PassPropertySchemaInitialized)
    {
        GbDetail2PassPropertySchemaInitialized = true;
        ConstructPropertyRecords(GDetail2PassPropertySchema, 13);
        std::atexit(DestroyDetail2PassPropertySchema);
    }
    // The second retail guard tests record zero Current.Type, independently.
    if (GDetail2PassPropertySchema[0].Current.Type != 0)
        return;
    InitializeDetail2PassPropertyRecords(GDetail2PassPropertySchema);
}

// LAB_06eb7ce0/7d00: independently recovered from every store and raw PE payload.
void DestroyDetailMapPropertySchema()
{
    DestroyPropertyRecords(GDetailMapPropertySchema, 13);
}

void InitializeDetailMapPropertySchema()
{
    if (!GbDetailMapPropertySchemaInitialized)
    {
        GbDetailMapPropertySchemaInitialized = true;
        ConstructPropertyRecords(GDetailMapPropertySchema, 13);
        std::atexit(DestroyDetailMapPropertySchema);
    }
    if (GDetailMapPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[0], 0x0, "Ac", "Ambient Color",
                                3, Payload_0_0, 3, Payload_0_1, 3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x41200000, 0x41200000, 0x41200000};
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[1], 0x0, "Dc", "Diffuse Color",
                                3, Payload_1_0, 3, Payload_1_1, 3, Payload_1_2);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[2], 0x0, "Dt_name", "Diffuse Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[3], 0x0, "Dt_flags", "Diffuse Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[4], 0x20000, "Dt_flags", "Diffuse Texture U Address Mode",
                                9, (void*)2, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[5], 0x40000, "Dt_flags", "Diffuse Texture V Address Mode",
                                9, (void*)2, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[6], 0x0, "flip u", "Flip U ",
                                1, nullptr, 1, nullptr, 1, (void*)1);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[7], 0x0, "flip v", "Flip V ",
                                1, nullptr, 1, nullptr, 1, (void*)1);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[8], 0x0, "Dm_name", "Detail Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[9], 0x0, "Dm_flags", "Detail Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[10], 0x20000, "Dm_flags", "Detail Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[11], 0x40000, "Dm_flags", "Detail Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    static const unsigned long Payload_12_0[] = {0x40800000};
    static const unsigned long Payload_12_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_12_2[] = {0x7F7FFFFF};
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[12], 0x0, "TileRate", "Tile Rate ",
                                2, Payload_12_0, 2, Payload_12_1, 2, Payload_12_2);
}

void DestroyBtDetailPropertySchema()
{
    DestroyPropertyRecords(GBtDetailPropertySchema, 13);
}

// The fourteenth Bt descriptor overlaps Atmosphere by one word. Keep each
// aliased tag/payload write live during foreign Release, without forming a
// second C++ record object over the first one's storage.
void AssignOverlappingZeroAddressMode(void* TypeAddress, unsigned long& Payload)
{
    unsigned long PreviousType;
    std::memcpy(&PreviousType, TypeAddress, sizeof(PreviousType));
    if (PreviousType != 9)
    {
        if (PreviousType == 5 || PreviousType == 7)
        {
            if (Payload != 0)
                HeapFree(GetProcessHeap(), 0, (void*)Payload);
        }
        else if (PreviousType == 6)
        {
            IDAComponent* Component = (IDAComponent*)Payload;
            Payload = 0;
            if (Component != nullptr)
                Component->Release();
        }
        Payload = 0;
    }
    const unsigned long NewType = 9;
    std::memcpy(TypeAddress, &NewType, sizeof(NewType));
    Payload = 0;
}

void InitializeBtDetailPropertySchema()
{
    if (!GbBtDetailPropertySchemaInitialized)
    {
        GbBtDetailPropertySchemaInitialized = true;
        ConstructPropertyRecords(GBtDetailPropertySchema, 13);
        std::atexit(DestroyBtDetailPropertySchema);
    }
    if (GBtDetailPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[0], 0x0, "Ac", "Ambient Color",
                                3, Payload_0_0, 3, Payload_0_1, 3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[1], 0x0, "Dc", "Diffuse Color",
                                3, Payload_1_0, 3, Payload_1_1, 3, Payload_1_2);
    static const unsigned long Payload_2_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[2], 0x0, "Ec", "Emissive Color",
                                3, Payload_2_0, 3, Payload_2_1, 3, Payload_2_2);
    static const unsigned long Payload_3_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[3], 0x0, "Sc", "Specular Color",
                                3, Payload_3_0, 3, Payload_3_1, 3, Payload_3_2);
    static const unsigned long Payload_4_0[] = {0x00000000};
    static const unsigned long Payload_4_1[] = {0x00000000};
    static const unsigned long Payload_4_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[4], 0x0, "Sp", "Specular Power",
                                2, Payload_4_0, 2, Payload_4_1, 2, Payload_4_2);
    static const unsigned long Payload_5_0[] = {0x3F800000};
    static const unsigned long Payload_5_1[] = {0x00000000};
    static const unsigned long Payload_5_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[5], 0x0, "Oc", "Opacity ",
                                2, Payload_5_0, 2, Payload_5_1, 2, Payload_5_2);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[6], 0x0, "Dt_name", "Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[7], 0x0, "Dt_flags", "Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[8], 0x20000, "Dt_flags", "Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[9], 0x40000, "Dt_flags", "Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[10], 0x0, "Bt_name", "DetailTexture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[11], 0x0, "Bt_flags", "DetailTexture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[12], 0x20000, "Bt_flags", "DetailTexture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);

    // 06eb4d94/4d99: flags alias the Atmosphere construction guard; the
    // following words overlap record zero and are published in setter order.
    GbAtmospherePropertySchemaInitialized = false;
    FShadingPropertyRecord& Atmosphere = GAtmospherePropertySchema[0];
    Atmosphere.Flags = (unsigned long)("Bt_flags");
    Atmosphere.Name = "DetailTexture V Address Mode";
    Atmosphere.Description = GPropertyMetadata_06ed56c4;
    AssignOverlappingZeroAddressMode(&Atmosphere.Callback, Atmosphere.Current.Type);
    AssignOverlappingZeroAddressMode(&Atmosphere.Current.Payload3, Atmosphere.Default.Type);
    AssignOverlappingZeroAddressMode(&Atmosphere.Default.Payload3, Atmosphere.Saved.Type);
}

// LAB_06ebc3f0 / LAB_06eba540: each own raw PE table verified independently.
void InitializeTexturePropertyRecords(FShadingPropertyRecord* Records)
{
    SetSinglePassPropertyRecord(Records[0], 0, "Dt_name", "Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(Records[1], 0, "Dt_flags", "Texture Wrap Mode",
                                8, (const void*)4UL, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(Records[2], 0x20000, "Dt_flags", "Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(Records[3], 0x40000, "Dt_flags", "Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
}

void DestroyNebulaPropertySchema()
{
    // FUN_06ebc720: four records in reverse Saved/Default/Current order.
    DestroyPropertyRecords(GNebulaPropertySchema, 4);
}

void InitializeNebulaPropertySchema()
{
    if (!GbNebulaPropertySchemaInitialized)
    {
        GbNebulaPropertySchemaInitialized = true;
        ConstructPropertyRecords(GNebulaPropertySchema, 4);
        std::atexit(DestroyNebulaPropertySchema);
    }
    if (GNebulaPropertySchema[0].Current.Type != 0)
        return;
    InitializeTexturePropertyRecords(GNebulaPropertySchema);
}

void DestroyLightMapPropertySchema()
{
    // FUN_06ebaab0: separate four-record table and registration.
    DestroyPropertyRecords(GLightMapPropertySchema, 4);
}

void InitializeLightMapPropertySchema()
{
    if (!GbLightMapPropertySchemaInitialized)
    {
        GbLightMapPropertySchemaInitialized = true;
        ConstructPropertyRecords(GLightMapPropertySchema, 4);
        std::atexit(DestroyLightMapPropertySchema);
    }
    if (GLightMapPropertySchema[0].Current.Type != 0)
        return;
    InitializeTexturePropertyRecords(GLightMapPropertySchema);
}

void InitializeEcEtPropertySchema()
{
    if (!GbEcEtPropertySchemaInitialized)
    {
        GbEcEtPropertySchemaInitialized = true;
        ConstructPropertyRecords(GEcEtPropertySchema, 5);
        std::atexit(DestroyEcEtPropertySchema);
    }
    if (GEcEtPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GEcEtPropertySchema[0], 0x4, "Ec", "Emissive Color",
                                3, Payload_0_0, 3, Payload_0_1, 3, Payload_0_2);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[1], 0x0, "Et_name", "Emissive Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[2], 0x0, "Et_flags", "Emissive Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[3], 0x20000, "Et_flags", "Emissive Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[4], 0x40000, "Et_flags", "Emissive Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
}

void InitializeDcDtEtPropertySchema()
{
    if (!GbDcDtEtPropertySchemaInitialized)
    {
        GbDcDtEtPropertySchemaInitialized = true;
        ConstructPropertyRecords(GDcDtEtPropertySchema, 9);
        std::atexit(DestroyDcDtEtPropertySchema);
    }
    if (GDcDtEtPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[0], 0x0, "Dc", "Diffuse Color",
                                3, Payload_0_0, 3, Payload_0_1, 3, Payload_0_2);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[1], 0x0, "Dt_name", "Diffuse Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[2], 0x0, "Dt_flags", "Diffuse Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[3], 0x20000, "Dt_flags", "Diffuse Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[4], 0x40000, "Dt_flags", "Diffuse Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[5], 0x0, "Et_name", "Emissive Texture Name",
                                7, "(None)", 7, nullptr, 7, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[6], 0x0, "Et_flags", "Emissive Texture Wrap Mode",
                                8, (void*)4, 8, nullptr, 8, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[7], 0x20000, "Et_flags", "Emissive Texture U Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[8], 0x40000, "Et_flags", "Emissive Texture V Address Mode",
                                9, nullptr, 9, nullptr, 9, nullptr);
}
} // namespace

FShadingPropertyRecord* GetBtDetailPropertySchema()
{
    InitializeBtDetailPropertySchema();
    return GBtDetailPropertySchema;
}

FShadingPropertyRecord* GetDetailMapPropertySchema()
{
    InitializeDetailMapPropertySchema();
    return GDetailMapPropertySchema;
}

FShadingPropertyRecord* GetAtmospherePropertySchema()
{
    InitializeAtmospherePropertySchema();
    return GAtmospherePropertySchema;
}

FShadingPropertyRecord* GetSinglePassPropertySchema()
{
    InitializeSinglePassPropertySchema();
    return GSinglePassPropertySchema;
}

FShadingPropertyRecord* GetIllumDetailPropertySchema()
{
    InitializeIllumDetailPropertySchema();
    return GIllumDetailPropertySchema;
}

FShadingPropertyRecord* GetEcEtPropertySchema()
{
    InitializeEcEtPropertySchema();
    return GEcEtPropertySchema;
}

FShadingPropertyRecord* GetDcDtEtPropertySchema()
{
    InitializeDcDtEtPropertySchema();
    return GDcDtEtPropertySchema;
}

FMaterialInner::FMaterialInner() {}

FMaterial* FMaterialInner::OwnerFromThis()
{
    return (FMaterial*)((unsigned char*)this -
                                        offsetof(FMaterial, Inner));
}

GENRESULT DACOM_API FMaterialInner::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ebe3a0; raw PE table 06ed28f0 = {IMaterial, 0, nullptr, 0}.
    FMaterial* Owner = OwnerFromThis();
    if (std::strcmp(Iid, Iids::Material) == 0)
    {
        Owner->AddRef();
        *Out = (IMaterial*)Owner;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FMaterialInner::AddRef()
{
    return ++OwnerFromThis()->RefCount;
}

unsigned long DACOM_API FMaterialInner::Release()
{
    // LAB_06eb65f0: the inner count never delegates to the aggregate outer.
    FMaterial* Owner = OwnerFromThis();
    if (Owner->RefCount != 0)
        --Owner->RefCount;
    const unsigned long Remaining = Owner->RefCount;
    if (Remaining == 0)
    {
        Owner->RefCount = 1;
        Owner->ScalarDeletingDestructor(1);
    }
    return Remaining;
}

FMaterial::FMaterial(const char* InFactoryName, const char* InClassName, EMaterialBind InBindKind,
                     EMaterialSubmit InSubmitKind)
    : Pipeline(nullptr),
      TexLib(nullptr),
      VBMgr(nullptr),
      RPDraw(nullptr),
      InstanceName(nullptr),
      TexBlocks(nullptr),
      TexCount(0),
      DiffuseR(1.0f),
      DiffuseG(1.0f),
      DiffuseB(1.0f),
      Opacity(1.0f),
      SpecularPower(0.0f),
      bAlphablend(0),
      bAlphatest(0),
      MatAnim(nullptr),
      Pad6C(0),
      bAtmosphereReady(0),
      FadeOff(1.0f),
      Alpha(0.75f),
      Scale(1.025f),
      DetailFlipU(0),
      DetailFlipV(0),
      DetailTileRate0(4.0f),
      DetailTileRate1(4.0f),
      DetailPassCount(2),
      bDetailSecondStageVerified(true),
      bDetailMultiPassEnabled(true),
      RefCount(1),
      FactoryName(InFactoryName),
      ClassNameText(InClassName),
      BindKind(InBindKind),
      SubmitKind(InSubmitKind),
      Outer(nullptr),
      bSecondaryTextureEnabled(true),
      bSecondaryTextureVerified(true),
      TextureAllocationCount(0)
{
    Ambient[0] = 1.0f;
    Ambient[1] = 1.0f;
    Ambient[2] = 1.0f;
    Ambient[3] = 1.0f;
    Specular[0] = 0.0f;
    Specular[1] = 0.0f;
    Specular[2] = 0.0f;
    Specular[3] = 0.0f;
    Emissive[0] = 0.0f;
    Emissive[1] = 0.0f;
    Emissive[2] = 0.0f;
    Emissive[3] = 0.0f;
    Pad66[0] = 0;
    Pad66[1] = 0;
    Pad75[0] = 0;
    Pad75[1] = 0;
    Pad75[2] = 0;
    {
        const unsigned long ScaleBits = 0x3F833333;
        std::memcpy(&Scale, &ScaleBits, sizeof(Scale));
    }

}

FMaterial::~FMaterial() noexcept(false)
{
    const bool bSinglePass = IsSinglePassPropertySchemaFamily(FactoryName);
    // FUN_06ebe490: the held animation is released before any texture group.
    if (bSinglePass && MatAnim != nullptr)
    {
        MatAnim->Release();
        MatAnim = nullptr;
    }
    // FUN_06ebc150: forward group release uses the live count.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        if (TexBlocks[Index].TextureId != -1)
        {
            TexLib->DecRefById(TexBlocks[Index].TextureId);
            TexBlocks[Index].TextureId = -1;
        }
    }
    if (TexBlocks != nullptr)
    {
        // Reverse name destruction uses the allocation's count, not TexCount.
        for (unsigned long Index = TextureAllocationCount; Index != 0; --Index)
        {
            delete[] TexBlocks[Index - 1].Name;
            TexBlocks[Index - 1].Name = nullptr;
        }
        delete[] TexBlocks;
    }
    TexBlocks = nullptr;
    TexLib = nullptr;
    Pipeline = nullptr;
    VBMgr = nullptr;
    delete[] InstanceName;
    InstanceName = nullptr;
}

static I3DMathEngine* G_MathEngine = nullptr;

void FMaterial::SetInstanceName(const char* Name)
{
    SetName(Name);
}

static const float kAtmosphereOne = 1.0f;
static const float kAtmosphereHalf = 0.5f;
static const unsigned long kAtmosphereEpsBits = 0x33D6BF95;

static_assert(offsetof(FMaterial, Pipeline) == 0x04, "Pipeline +4");
static_assert(offsetof(FMaterial, TexLib) == 0x08, "TexLib +8");
static_assert(offsetof(FMaterial, VBMgr) == 0x0C, "VBMgr +C");
static_assert(offsetof(FMaterial, RPDraw) == 0x10, "IRPDraw +10");
static_assert(offsetof(FMaterial, InstanceName) == 0x14, "instance name +14");
static_assert(offsetof(FMaterial, TexBlocks) == 0x18, "texblock +18");
static_assert(offsetof(FMaterial, TexCount) == 0x1C, "tex count +1C");
static_assert(offsetof(FMaterial, DiffuseR) == 0x20, "Diffuse +20");
static_assert(offsetof(FMaterial, Opacity) == 0x2C, "Opacity +2C");
static_assert(offsetof(FMaterial, Ambient) == 0x30, "Ambient +30");
static_assert(offsetof(FMaterial, Specular) == 0x40, "Specular +40");
static_assert(offsetof(FMaterial, Emissive) == 0x50, "Emissive +50");
static_assert(offsetof(FMaterial, SpecularPower) == 0x60, "SpecularPower +60");
static_assert(offsetof(FMaterial, bAlphablend) == 0x64, "Oc flag +64");
static_assert(offsetof(FMaterial, bAlphatest) == 0x65, "alphatest +65");
static_assert(offsetof(FMaterial, MatAnim) == 0x68, "MatAnim +68");
static_assert(offsetof(FMaterial, Inner) == 0x70, "inner QI +70");
static_assert(offsetof(FMaterial, bAtmosphereReady) == 0x74, "Atmosphere +74");

void FMaterial::ConfigureAggregation(FMaterialAggregateDesc* Desc)
{
    Outer = Desc->OuterAt8;
    if (Outer != nullptr)
        *Desc->InnerOutAtC = (IDAComponent*)(&Inner);
}

GENRESULT DACOM_API FMaterial::QueryInterface(const char* Iid, void** Out)
{
    return Outer != nullptr ? Outer->QueryInterface(Iid, Out) : Inner.QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMaterial::AddRef()
{
    return Outer != nullptr ? Outer->AddRef() : Inner.AddRef();
}

unsigned long DACOM_API FMaterial::Release()
{
    return Outer != nullptr ? Outer->Release() : Inner.Release();
}

GENRESULT DACOM_API FMaterial::InnerQueryInterface(const char* Iid, void** Out)
{
    return Inner.QueryInterface(Iid, Out);
}
unsigned long DACOM_API FMaterial::InnerAddRef()
{
    return Inner.AddRef();
}
unsigned long DACOM_API FMaterial::InnerRelease()
{
    return Inner.Release();
}

GENRESULT DACOM_API FMaterial::Initialize(IDAComponent* Component)
{
    // FUN_06eb1200: these are borrowed component pointers after immediate Release.
    if (Component == nullptr)
        return GR_GENERIC;
    if (Component->QueryInterface(Iids::TextureLibraryA, (void**)&TexLib) < 0)
        return GR_GENERIC;
    TexLib->Release();
    if (Component->QueryInterface(Iids::RenderPipeline8B, (void**)(&Pipeline)) < 0)
        return GR_GENERIC;
    Pipeline->Release();
    if (Component->QueryInterface(Iids::RPDraw, (void**)&RPDraw) < 0)
        return GR_GENERIC;
    RPDraw->Release();
    if (Component->QueryInterface(Iids::VertexBufferManager, (void**)&VBMgr) < 0)
        return GR_GENERIC;
    VBMgr->Release();

    return InitializeOwnedSchema();
}

GENRESULT FMaterial::InitializeOwnedSchema()
{
    void* Table;
    const unsigned long Count = GetParameterTable(&Table);
    FShadingPropertyRecord* Records = (FShadingPropertyRecord*)Table;
    TexCount = 0;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        if (Records[Index].Current.Type == 7)
            ++TexCount;
    }
    if (TexCount != 0)
    {
        TextureAllocationCount = TexCount;
        TexBlocks = new (std::nothrow) FTexBlock[TexCount];
        if (TexBlocks != nullptr)
        {
            for (unsigned long Index = 0; Index < TexCount; ++Index)
            {
                TexBlocks[Index].Name = nullptr;
                TexBlocks[Index].TextureId = -1;
                TexBlocks[Index].Handle = 0;
                TexBlocks[Index].Flags = 4;
                TexBlocks[Index].AddressU = 0;
                TexBlocks[Index].AddressV = 0;
                TexBlocks[Index].bSkipResolve = 0;
            }
        }
    }
    for (unsigned long Index = 0; Index < Count; ++Index)
        StoreParameterValue(Index, &Records[Index], &Records[Index].Current);
    return GR_OK;
}

namespace
{
    // FUN_06eb3450 / FUN_06eb3510: scalar output is committed only on full success.
    GENRESULT ReadSinglePassWord(IFileSystem* FileSystem, const char* Name, unsigned long* Output,
                                 bool bFloat = false)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        const int Handle = FileSystem->OpenByDesc((FFileSystemDesc*)(&Desc));
        if (Handle == -1)
            return GR_GENERIC;
        unsigned long ReadValue;
        unsigned long BytesRead;
        const int ReadResult = FileSystem->Read(Handle, &ReadValue, sizeof(ReadValue), &BytesRead, 0);
        const bool bSuccess = ReadResult != 0 && BytesRead == sizeof(ReadValue);
        if (bSuccess)
        {
            if (bFloat)
            {
                PortableFloatLoad::Copy(Output, &ReadValue);
            }
            else
                *Output = ReadValue;
        }
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : GR_GENERIC;
    }
}

namespace
{
    // FUN_06eb35d0: vector reads write directly; optional count is in float elements.
    GENRESULT ReadSinglePassVector(IFileSystem* FileSystem, const char* Name,
                                   unsigned long Count, void* Out, unsigned long* OutCount)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        const int Handle = FileSystem->OpenByDesc((FFileSystemDesc*)(&Desc));
        if (Handle == -1)
            return GR_GENERIC;
        if (OutCount != nullptr)
        {
            const unsigned long Available = FileSystem->GetSize(Handle, 0) >> 2;
            if (Available < Count)
                Count = Available;
            *OutCount = Count;
        }
        const unsigned long Size = Count * 4;
        unsigned long BytesRead;
        const int ReadResult = FileSystem->Read(Handle, Out, Size, &BytesRead, 0);
        const bool bSuccess = ReadResult != 0 && BytesRead == Size;
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : GR_GENERIC;
    }
}

namespace
{
    // FUN_06eb36b0 publishes allocation before Read, including on a failed read.
    GENRESULT ReadSinglePassString(IFileSystem* FileSystem, const char* Name,
                                   unsigned long Size, char** Out)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        const int Handle = FileSystem->OpenByDesc((FFileSystemDesc*)(&Desc));
        if (Handle == -1)
            return GR_GENERIC;
        if (Size == 0)
            Size = FileSystem->GetSize(Handle, 0);
        char* Storage = new (std::nothrow) char[Size];
        bool bSuccess = false;
        if (Storage != nullptr)
        {
            *Out = Storage;
            unsigned long BytesRead;
            const int ReadResult = FileSystem->Read(Handle, Storage, Size, &BytesRead, 0);
            bSuccess = ReadResult != 0 && BytesRead == Size;
        }
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : GR_GENERIC;
    }
}

GENRESULT FMaterial::LoadDeclaredSchema(IFileSystem* FileSystem)
{
    // LAB_06eb1370: walk the actual schema in order; no aliases or early texture resolve.
    void* Table;
    const unsigned long Count = GetParameterTable(&Table);
    FShadingPropertyRecord* Records = (FShadingPropertyRecord*)Table;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        FShadingPropertyRecord& Record = Records[Index];
        if ((Record.Flags & 1) != 0)
            continue;
        FShadingPropertyValue Value = {};
        Value.Type = Record.Current.Type;
        GENRESULT Result = GR_OK;
        switch (Value.Type)
        {
        case 1:
            // 13f2..146b opens the named FileSystem leaf and reads one dword.
            Result = ReadSinglePassWord(FileSystem, Record.Name, &Value.Payload0);
            break;
        case 2:
            Result = ReadSinglePassWord(FileSystem, Record.Name, &Value.Payload0, true);
            break;
        case 3:
        case 4:
            Result = ReadSinglePassVector(FileSystem, Record.Name, Value.Type == 3 ? 3 : 4,
                                          &Value.Payload0, nullptr);
            break;
        case 5:
        case 7:
        {
            char* RawString;
            Result = ReadSinglePassString(FileSystem, Record.Name, 0, &RawString);
            if (Result >= 0)
            {
                Value.Assign(Value.Type, RawString);
                delete[] RawString;
            }
            // Retail's failed read does not transfer RawString into Value.
            break;
        }
        case 8:
        case 9:
        {
            unsigned long Packed;
            Result = ReadSinglePassWord(FileSystem, Record.Name, &Packed);
            if (Result >= 0)
            {
                if (Value.Type == 8)
                    Value.Payload0 = (Packed >> 4) & 0xF;
                else
                {
                    const bool bV = (Record.Flags & 0x20000) == 0 &&
                                    (Record.Flags & 0x40000) != 0;
                    Value.Payload0 = (Packed >> (bV ? 2 : 0)) & 3;
                }
            }
            break;
        }
        default:
            // Retail type6 and unknown tags fall through to the property store.
            break;
        }
        if (Result >= 0)
            StoreParameterValue(Index, &Record, &Value);
        Value.ReleaseOwnedPayload();
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::LoadFromFileSystem(void* FileSystem)
{
    // LAB_06eb1370: every family walks its actual table, including Null's zero count.
    return LoadDeclaredSchema((IFileSystem*)FileSystem);
}

namespace
{
    // FUN_06eb3780 (word), 3840 (float elements), 3900 (bytes).
    GENRESULT WriteMaterialPropertyBytes(IFileSystem* FileSystem, const char* Name,
                                         const void* Buffer, unsigned long Size)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        Desc.Implementation = "UTF";
        Desc.Access = 0xC0000000;
        Desc.Creation = 2;
        const int Handle = FileSystem->OpenByDesc(&Desc);
        if (Handle == -1)
            return (GENRESULT)0x80004005UL;
        unsigned long BytesWritten;
        const int WriteResult = FileSystem->Write(
            (unsigned long)Handle, Buffer, Size, &BytesWritten, nullptr);
        const bool bSuccess = WriteResult != 0 && BytesWritten == Size;
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : (GENRESULT)0x80004005UL;
    }
}

GENRESULT DACOM_API FMaterial::SaveProperties(void* FileSystem)
{
    // FUN_06eb1720: table-driven snapshot, then write, then typed cleanup.
    void* Table;
    const unsigned long Count = GetParameterTable(&Table);
    FShadingPropertyRecord* Records = (FShadingPropertyRecord*)Table;
    IFileSystem* Files = (IFileSystem*)FileSystem;
    GENRESULT Result = GR_OK;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        FShadingPropertyRecord& Record = Records[Index];
        if ((Record.Flags & 1) != 0)
            continue;
        FShadingPropertyValue Value = {};
        Value.Type = Record.Current.Type;
        const unsigned long* Target = (const unsigned long*)(GetParameterAddress(Index));
        if (Target == nullptr)
        {
            Value.ReleaseOwnedPayload();
            Result = GR_GENERIC;
            continue;
        }
        switch (Value.Type)
        {
        case 4:
            Value.Payload3 = Target[3];
            [[fallthrough]];
        case 3:
            Value.Payload2 = Target[2];
            Value.Payload1 = Target[1];
            [[fallthrough]];
        default:
            Value.Payload0 = Target[0];
            break;
        case 5:
        case 7:
        case 6:
            Value.Assign(Value.Type, (const void*)(Target[0]));
            break;
        }
        GENRESULT WriteResult = GR_OK;
        switch (Value.Type)
        {
        case 1:
        case 2:
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, &Value.Payload0, 4);
            break;
        case 3:
        case 4:
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, &Value.Payload0,
                                                     Value.Type == 3 ? 12 : 16);
            break;
        case 5:
        case 7:
        {
            const char* Text = (const char*)(Value.Payload0);
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, Text,
                (unsigned long)(std::strlen(Text) + 1));
            break;
        }
        case 8:
        case 9:
        {
            unsigned long Packed = 0;
            ReadSinglePassWord(Files, Record.Name, &Packed); // Read failure is ignored.
            if (Value.Type == 8)
                Packed = (Packed & ~0xF0UL) | ((Value.Payload0 << 4) & 0xF0UL);
            else
            {
                const bool bV = (Record.Flags & 0x20000) == 0 &&
                                (Record.Flags & 0x40000) != 0;
                if (!bV)
                    Packed = (Packed & ~3UL) | (Value.Payload0 & 3UL);
                // Retail 196c..1979 jumps to 1936: U also writes the V bits.
                Packed = (Packed & ~0xCUL) | ((Value.Payload0 << 2) & 0xCUL);
            }
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, &Packed, 4);
            break;
        }
        default:
            // Interface/tag0/10/future values are snapshotted and cleaned but not written.
            break;
        }
        if (WriteResult < 0)
            Result = GR_GENERIC;
        Value.ReleaseOwnedPayload();
    }
    return Result;
}

GENRESULT FMaterial::ApplyVerifyMatAnim()
{
    // LAB_06ebd810..d8ac: retain the QI output across the whole verification.
    IMatAnimLibrary* AnimationLibrary = nullptr; // formerly LibraryRaw
    const GENRESULT QueryResult = Pipeline->QueryInterface(Iids::MatAnimLibrary, (void**)&AnimationLibrary);
    if (QueryResult >= 0)
    {
        unsigned long Query[2];
        Query[0] = (unsigned long)InstanceName;
        Query[1] = DACOM_CRC::GetCRC32(InstanceName);
        if (MatAnim != nullptr)
        {
            MatAnim->Release();
            MatAnim = nullptr;
        }
        AnimationLibrary->CreateAnimationInstance(-1, Query, &MatAnim);
    }
    Setup();
    Bind(nullptr);
    const GENRESULT Result = Pipeline->VerifyState();
    Teardown(nullptr);
    if (AnimationLibrary != nullptr)
        AnimationLibrary->Release();
    return Result;
}

GENRESULT FMaterial::ApplyVerifyDetailFamily()
{
    // LAB_06eb6ef0 / LAB_06ebb680: two verifies, then single-pass fallback.
    DetailPassCount = 2;
    bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bDetailSecondStageVerified = true;
    if (SubmitKind == SubmitDetail2Pass)
        ApplyDetailPassState(0);
    else
        ApplyMaskedDetailPassState(0);
    const bool bFirstVerified = Pipeline->VerifyState() >= 0;
    if (SubmitKind == SubmitDetail2Pass)
        ApplyDetailPassState(1);
    else
        ApplyMaskedDetailPassState(1);
    GENRESULT Result = Pipeline->VerifyState();
    const bool bSecondVerified = Result >= 0;
    bDetailSecondStageVerified = bFirstVerified && bSecondVerified;
    if (!bFirstVerified || !bSecondVerified || !bDetailMultiPassEnabled)
    {
        if (TexBlocks[1].TextureId != -1)
        {
            TexLib->DecRefById(TexBlocks[1].TextureId);
            TexBlocks[1].TextureId = -1;
        }
        bDetailSecondStageVerified = false;
        bDetailMultiPassEnabled = false;
        DetailPassCount = 1;
        if (SubmitKind == SubmitDetail2Pass)
            ApplyDetailPassState(0);
        else
            ApplyMaskedDetailPassState(0);
        Result = Pipeline->VerifyState();
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT FMaterial::ApplyVerifyIllumDetail()
{
    // LAB_06eb9920 returns zero on accepted pass configurations, not Verify's value.
    DetailPassCount = 2;
    bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bool bFirstVerified = false;
    bool bSecondVerified = false;
    bDetailSecondStageVerified = true;
    if (bDetailMultiPassEnabled)
    {
        ApplyIllumDetailPassState(0);
        bFirstVerified = Pipeline->VerifyState() >= 0;
        ApplyIllumDetailPassState(1);
        bSecondVerified = Pipeline->VerifyState() >= 0;
    }
    bDetailSecondStageVerified = bFirstVerified && bSecondVerified;
    GENRESULT Result = GR_OK;
    if (!bFirstVerified || !bSecondVerified || !bDetailMultiPassEnabled)
    {
        DetailPassCount = 1;
        bDetailSecondStageVerified = bFirstVerified;
        if (!bFirstVerified || !bDetailMultiPassEnabled)
        {
            if (TexBlocks[1].TextureId != -1)
            {
                TexLib->DecRefById(TexBlocks[1].TextureId);
                TexBlocks[1].TextureId = -1;
            }
            DetailPassCount = 1;
            bDetailSecondStageVerified = false;
            bDetailMultiPassEnabled = false;
            ApplyIllumDetailPassState(0);
            Result = Pipeline->VerifyState();
        }
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FMaterial::ApplyVerify(void* Context, void* Extra)
{
    if (BindKind == BindNull)
        return GR_OK;
    if (IsSinglePassPropertySchemaFamily(FactoryName))
        return ApplyVerifyMatAnim();
    if (SubmitKind == SubmitDetail2Pass || SubmitKind == SubmitMaskedDetail2Pass)
        return ApplyVerifyDetailFamily();
    if (SubmitKind == SubmitIllumDetail2Pass)
        return ApplyVerifyIllumDetail();
    if (BindKind == BindBtDetail)
    {
        // LAB_06eb52b0: global base+4 is record 0.Value.
        bSecondaryTextureEnabled = GetMaterialGlobalRecordValue(0) >= 1;
        Setup();
        bSecondaryTextureVerified = true;
        Bind(nullptr);
        GENRESULT Result = Pipeline->VerifyState();
        bSecondaryTextureVerified = Result >= 0;
        if (!bSecondaryTextureVerified || !bSecondaryTextureEnabled)
        {
            FTexBlock& Secondary = TexBlocks[1];
            if (Secondary.TextureId != -1)
            {
                TexLib->DecRefById(Secondary.TextureId);
                Secondary.TextureId = -1;
            }
            // 5325 Bind precedes both readiness clears at 532B/532F.
            Bind(nullptr);
            bSecondaryTextureVerified = false;
            bSecondaryTextureEnabled = false;
            Result = Pipeline->VerifyState();
        }
        Teardown(nullptr);
        return Result;
    }
    if (BindKind == BindDcDtEt)
    {
        // LAB_06eb63b0: global base+14 is record 2.Value.
        bSecondaryTextureEnabled = GetMaterialGlobalRecordValue(2) >= 1;
        Setup();
        Bind(nullptr);
        GENRESULT Result = Pipeline->VerifyState();
        if (Result < 0 || !bSecondaryTextureEnabled)
        {
            FTexBlock& Secondary = TexBlocks[1];
            if (Secondary.TextureId != -1)
            {
                TexLib->DecRefById(Secondary.TextureId);
                Secondary.TextureId = -1;
            }
            if (bSecondaryTextureEnabled)
            {
                Bind(nullptr);
                Result = Pipeline->VerifyState();
            }
        }
        Teardown(nullptr);
        return Result;
    }
    if (BindKind == BindDetailMap)
    {
        // LAB_06eb80d0 belongs to DetailMapMaterial, with two distinct flags.
        bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
        Setup();
        bDetailSecondStageVerified = true;
        Bind(nullptr);
        GENRESULT Result = Pipeline->VerifyState();
        bDetailSecondStageVerified = Result >= 0;
        if (!bDetailSecondStageVerified || !bDetailMultiPassEnabled)
        {
            FTexBlock& Detail = TexBlocks[1];
            if (Detail.TextureId != -1)
            {
                TexLib->DecRefById(Detail.TextureId);
                Detail.TextureId = -1;
            }
            Bind(nullptr);
            bDetailSecondStageVerified = false;
            bDetailMultiPassEnabled = false;
            Result = Pipeline->VerifyState();
        }
        Teardown(nullptr);
        return Result;
    }
    if (BindKind == BindAtmosphere)
    {
        // LAB_06eb2cb0 verifies once, preserving the resolved readiness byte.
        Setup();
        const unsigned char SavedReady = bAtmosphereReady;
        bAtmosphereReady = 1;
        Bind(nullptr);
        bAtmosphereReady = SavedReady;
        const GENRESULT Result = Pipeline->VerifyState();
        if (Result != GR_OK)
        {
            char Message[0x2000];
            std::sprintf(Message, "failed to verify %s", InstanceName);
            FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\AtmosphereMaterial.cpp",
                0x97, Message);
        }
        Teardown(nullptr);
        return Result;
    }
    // LAB_06eb3aa0: the remaining families use the strict shared lifecycle.
    Setup();
    Bind(nullptr);
    const GENRESULT Result = Pipeline->VerifyState();
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FMaterial::ResolveTextures(float /*Delta*/)
{
    if (BindKind == BindAtmosphere)
    {
        // LAB_06eb2c40: unsigned count, mandatory block/library on lookup.
        for (unsigned long Index = 0; Index < TexCount; ++Index)
        {
            FTexBlock& Entry = TexBlocks[Index];
            if (Entry.TextureId == -1)
                Entry.Handle = 0;
            else if (Entry.bSkipResolve == 0)
                TexLib->GetFrame(Entry.TextureId, -1, &Entry.Handle);
        }
        // +0xC from the record base is record 1.Value, not Active.
        bAtmosphereReady = GetMaterialGlobalRecordValue(1) >= 1 ? 1 : 0;
        return GR_OK;
    }
    // LAB_06eb3ae0: stride 0x2C, TexLib +0x70(id, -1, out=+8). id==-1 → handle 0.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        FTexBlock* Entry = &TexBlocks[Index];
        if (Entry->TextureId == -1)
        {
            Entry->Handle = 0;
            continue;
        }
        if (Entry->bSkipResolve != 0)
            continue;
        TexLib->GetFrame(Entry->TextureId, -1, &Entry->Handle);
    }
    if (BindKind == BindDcDtEt)
        bSecondaryTextureEnabled = GetMaterialGlobalRecordValue(2) >= 1;
    return GR_OK;
}

void FMaterial::BindDcDtTables(void* Context)
{
    // FUN_06ebd8b0: all SinglePass variants share this body.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed26dc, true, 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed2714, true, 1);
    ApplyDetailTable(Pipeline, bAlphablend != 0 ? GBindTable_06ed26ac :
                     GBindTable_06ed268c, false, 0);
    if (MatAnim != nullptr)
    {
        float Matrix[16];
        IImaInstance* Instance = MatAnim;
        if (Instance->FillUvMatrix(Matrix) != 0)
        {
            Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
            Pipeline->SetTextureStageTransform(0, Matrix);
        }
    }
}

void FMaterial::BindLightMapTables(void* Context)
{
    // FUN_06eba720: 123 own instructions, unconditional stage tables.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    ApplyDetailTable(Pipeline, GBindTable_06ed209c, false, 0);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed20ac, true, 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed20e4, true, 1);
    ApplyDetailTable(Pipeline, GBindTable_06ed206c, false, 0);
}

void FMaterial::BindNebulaTables(void* Context)
{
    // FUN_06ebc600: material first, then the optional mesh COLORVERTEX write.
    SetMaterialBlock(Pipeline, &DiffuseR);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            PipeSetRs(Pipeline, kRsColorVertex, 1);
    }
    FTexBlock* Block = &TexBlocks[0];
    PipeSetTexture(Pipeline, 0, Block->Handle);
    PipeSetTss(Pipeline, 0, kTssTexCoordIndex, Block->Flags < 4 ? 0 : Block->Flags - 4);
    PipeSetTss(Pipeline, 0, kTssAddressU, Block->AddressU + 1);
    PipeSetTss(Pipeline, 0, kTssAddressV, Block->AddressV + 1);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss0, true, 0);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss1, true, 1);
    ApplyStateTable(Pipeline, GNebulaRs, false, 0);
}

void FMaterial::BindAtmosphereTables()
{
    // FUN_06eb2b40: caller tests readiness; dependencies are mandatory.
    SetMaterialBlock(Pipeline, &DiffuseR);
    FTexBlock* Block = &TexBlocks[0];
    PipeSetTexture(Pipeline, 0, Block->Handle);
    PipeSetTss(Pipeline, 0, kTssTexCoordIndex, Block->Flags < 4 ? 0 : Block->Flags - 4);
    PipeSetTss(Pipeline, 0, kTssAddressU, Block->AddressU + 1);
    PipeSetTss(Pipeline, 0, kTssAddressV, Block->AddressV + 1);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss0, true, 0);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss1, true, 1);
    ApplyStateTable(Pipeline, GAtmosphereRs, false, 0);
}

void FMaterial::BindDcDtEtTables(void* Context)
{
    // FUN_06eb61e0: enabled flag and the SECOND block's id control stage 1.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    ApplyDetailTable(Pipeline, GBindTable_06ed16cc, false, 0);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed16ec, true, 0);
    unsigned long DisabledStage = 1;
    if (bSecondaryTextureEnabled && TexBlocks[1].TextureId != -1)
    {
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GBindTable_06ed1724, true, 1);
        DisabledStage = 2;
    }
    ApplyDetailTable(Pipeline, GBindTable_06ed175c, true, DisabledStage);
}

void FMaterial::BindEcEtTables(void* Context)
{
    // FUN_06eb8a40: mandatory material, optional marker, block0, fixed tables.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed1c84, true, 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed1cbc, true, 1);
    ApplyDetailTable(Pipeline, GBindTable_06ed1c54, false, 0);
}

void FMaterial::BindDetailMapTables(void* Context)
{
    // LAB_06eb81e0: mandatory material first, then optional mesh marker.
    SetMaterialBlock(Pipeline, &DiffuseR);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            Pipeline->SetRenderState(D3DRS_COLORVERTEX, 1);
    }
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1abc, true, 0);
    unsigned long DisabledStage = 1;
    if (bDetailSecondStageVerified)
    {
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GDetailMapTable_06ed1af4, true, 1);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
        // 8354..837b zero all sixteen floats, then write only [0] and [5].
        float Matrix[16] = {};
        Matrix[0] = DetailTileRate0;
        Matrix[5] = DetailTileRate0;
        Pipeline->SetTextureStageTransform(1, Matrix);
        DisabledStage = 2;
    }
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1b2c, true, DisabledStage);
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1a9c, false, 0);
}

void FMaterial::BindBtDetailTables(void* Context)
{
    // FUN_06eb5100: +69 alone selects the optional second-stage body.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    ApplyDetailTable(Pipeline, GBindTable_06ed1484, false, 0);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed14a4, true, 0);
    unsigned long DisabledStage = 1;
    if (bSecondaryTextureVerified)
    {
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GBindTable_06ed14dc, true, 1);
        DisabledStage = 2;
    }
    ApplyDetailTable(Pipeline, GBindTable_06ed1514, true, DisabledStage);
}

GENRESULT DACOM_API FMaterial::Bind(void* Context)
{
    // LAB_06ec9560: Null and the two-pass families bind without side effects.
    if (BindKind == BindNull || BindKind == BindStub)
        return GR_OK;

    if (BindKind == BindAtmosphere)
    {
        if (bAtmosphereReady != 0)
            BindAtmosphereTables();
        return GR_OK;
    }
    if (BindKind == BindNebula)
    {
        BindNebulaTables(Context);
        return GR_OK;
    }

    if (BindKind == BindDetailMap)
    {
        BindDetailMapTables(Context);
        return GR_OK;
    }

    if (IsSinglePassPropertySchemaFamily(FactoryName))
    {
        BindDcDtTables(Context);
        return GR_OK;
    }

    if (BindKind == BindLightMap)
    {
        BindLightMapTables(Context);
        return GR_OK;
    }

    if (BindKind == BindBtDetail)
    {
        BindBtDetailTables(Context);
        return GR_OK;
    }

    if (BindKind == BindDcDtEt)
    {
        BindDcDtEtTables(Context);
        return GR_OK;
    }

    if (BindKind == BindEcEt)
        BindEcEtTables(Context);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::Teardown(void* Context)
{
    if (BindKind == BindNull)
        return GR_OK;

    if (SubmitKind == SubmitDetail2Pass || SubmitKind == SubmitIllumDetail2Pass ||
        SubmitKind == SubmitMaskedDetail2Pass)
    {
        // 6e80 / 98b0 / b610 ignore Context and perform the same nine TSS writes.
        const unsigned long* Table = SubmitKind == SubmitDetail2Pass ? GDetailTable_06ed194c :
                                     SubmitKind == SubmitIllumDetail2Pass ? GDetailTable_06ed1f74 :
                                                                          GDetailTable_06ed22cc;
        ApplyDetailTable(Pipeline, Table, true, 1);
        Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
        return GR_OK;
    }

    // FUN_06eb50a0 resets all six stage-1 states before inspecting Context.
    if (BindKind == BindBtDetail)
        ApplyDetailTable(Pipeline, GBindTable_06ed1514, true, 1);

    // FUN_06ebda30: only SinglePass owns the animated TSS reset, before
    // reading Context. LAB_06eb1a10 has only the conditional COLORVERTEX reset.
    if (IsSinglePassPropertySchemaFamily(FactoryName) && MatAnim != nullptr)
        Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            Pipeline->SetRenderState(D3DRS_COLORVERTEX, 0);
    }
    return GR_OK;
}

static I3DMathEngine* AcquireMathEngine()
{
    // FUN_06ed0b10 creates on every call; each caller owns its lazy global.
    FMathDesc Desc;
    Desc.Size = sizeof(Desc);
    Desc.InterfaceName = "3DMathEngine";
    Desc.Implementation = nullptr;
    void* Created;
    ICOManager* Manager = DACOM_Acquire();
    Manager->CreateInstance((AGGDESC*)(&Desc), &Created);
    return (I3DMathEngine*)Created;
}

static float AtmosphereEps()
{
    float Value;
    std::memcpy(&Value, &kAtmosphereEpsBits, sizeof(Value));
    return Value;
}

static void NegateTwoPassNormals(const FRpVertexSrcDesc* Source, unsigned long Start,
                                 unsigned long Count, void* Vertices, unsigned long DestStride,
                                 unsigned long DestPosSize)
{
    // LAB_06ebe1ff: read original source normals, not the copied destination.
    uintptr_t Destination = (uintptr_t)Vertices + DestPosSize;
    if ((Source->Flags & 4) != 0)
    {
        const unsigned long* Indices = Source->Normal.IndexArray + Start;
        for (unsigned long Index = 0; Index < Count; ++Index)
        {
            const float* Normal = (const float*)(
                (uintptr_t)(Source->Normal.Base) + Indices[Index] * Source->Normal.Stride);
            float* Out = (float*)Destination;
            Out[0] = -Normal[0];
            Out[1] = -Normal[1];
            Out[2] = -Normal[2];
            Destination += DestStride;
        }
        return;
    }
    const unsigned long SourceStride = FvfPosSize(Source->SrcFvf) + FvfNormalSize(Source->SrcFvf) +
                                       FvfColorSize(Source->SrcFvf) + FvfTexSize(Source->SrcFvf);
    uintptr_t NormalAddress = (uintptr_t)(Source->Normal.Base) + SourceStride * Start;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        const float* Normal = (const float*)NormalAddress;
        float* Out = (float*)Destination;
        Out[0] = -Normal[0];
        Out[1] = -Normal[1];
        Out[2] = -Normal[2];
        NormalAddress += SourceStride;
        Destination += DestStride;
    }
}

GENRESULT FMaterial::SubmitDcDtTwoSecondPass(void* Context, unsigned long PrimType, unsigned long Start,
                                            unsigned long Count, unsigned long IbStart, unsigned long IndexSpan)
{
    // LAB_06ebe0d2..06ebe38d: the caller has already tested first result and source presence.
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    const FRpVertexSrcDesc* Source = SubmitContext->SourceVertices;
    IVertexBufferManager* Manager = VBMgr;
    FVertexMemoryLock Lock;
    const GENRESULT Locked = Manager->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Locked < 0)
        return Locked;
    const unsigned long DestPosSize = FvfPosSize(Lock.Listing.Fvf);
    const unsigned long DestStride = DestPosSize + FvfNormalSize(Lock.Listing.Fvf) +
                                     FvfColorSize(Lock.Listing.Fvf) + FvfTexSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) - DestStride * Start);
    Manager->CopyVertexData(Destination, Lock.Listing.Fvf,
                           SubmitContext->SourceVertices, Start,
                           Count);
    if ((Source->SrcFvf & 0x10) != 0)
        NegateTwoPassNormals(Source, Start, Count, Lock.Vertices, DestStride, DestPosSize);
    Manager->UnlockVertexMemory(&Lock);
    int SavedCull;
    Pipeline->GetRenderState(D3DRS_CULLMODE, &SavedCull);
    // Raw PE 06ebe332 operand points at 06ed4848: {0,1,3,2}.
    Pipeline->SetRenderState(D3DRS_CULLMODE, kCullFlip[SavedCull]);
    const GENRESULT Result = Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer,
        Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(SubmitContext->Indices) + IbStart * 2),
        IndexSpan, 0);
    Pipeline->SetRenderState(D3DRS_CULLMODE, SavedCull);
    return Result;
}

// DAT_06ed44cc aliases the Ec string as word zero of this cull lookup.
static const unsigned long GBtDetailTwoCullFlip[4] = {0x00006345, 1, 3, 2};

GENRESULT FMaterial::SubmitBtDetailTwoSecondPass(void* Context, unsigned long PrimType,
                                                unsigned long Start, unsigned long Count,
                                                unsigned long IbStart, unsigned long IndexSpan)
{
    // LAB_06eb5655..593a: no alpha-state scope surrounds this second pass.
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    const FRpVertexSrcDesc* Source = SubmitContext->SourceVertices;
    FVertexMemoryLock Lock;
    const GENRESULT Locked = VBMgr->LockVertexMemory(
        Source->SrcFvf, Count, &Lock);
    if (Locked < 0)
        return Locked;
    Source = SubmitContext->SourceVertices;
    const bool bHasNormals = (Source->SrcFvf & 0x10) != 0;
    const unsigned long DestPosSize = FvfPosSize(Lock.Listing.Fvf);
    const unsigned long DestStride = DestPosSize + FvfNormalSize(Lock.Listing.Fvf) +
                                     FvfColorSize(Lock.Listing.Fvf) + FvfTexSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) -
                                                DestStride * Start);
    VBMgr->CopyVertexData(
        Destination, Lock.Listing.Fvf,
        SubmitContext->SourceVertices, Start,
        Count);
    if (bHasNormals)
    {
        // 5793 reads the descriptor again after the foreign CopyVertexData callback.
        Source = SubmitContext->SourceVertices;
        NegateTwoPassNormals(Source, Start, Count, Lock.Vertices, DestStride, DestPosSize);
    }
    VBMgr->UnlockVertexMemory(&Lock);
    int SavedCull;
    Pipeline->GetRenderState(D3DRS_CULLMODE, &SavedCull);
    Pipeline->SetRenderState(D3DRS_CULLMODE, GBtDetailTwoCullFlip[SavedCull]);
    const GENRESULT Result = Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer,
        Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(SubmitContext->Indices) +
                                           IbStart * 2), IndexSpan, 0);
    Pipeline->SetRenderState(D3DRS_CULLMODE, SavedCull);
    return Result;
}

GENRESULT FMaterial::SubmitAtmosphereLock(void* Context, unsigned long PrimType, unsigned long Start,
                                          unsigned long Count, unsigned long IbStart, unsigned long IndexSpan)
{
    // LAB_06eb2d40: readiness is the only early success branch.
    if (bAtmosphereReady == 0)
        return GR_OK;
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    const FRpVertexSrcDesc* Source = SubmitContext->SourceVertices;
    IVertexBufferManager* Manager = VBMgr;
    FVertexMemoryLock Lock;
    const GENRESULT Locked = Manager->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Locked < 0)
        return Locked;
    const unsigned long DestStride = FvfTexSize(Lock.Listing.Fvf) + FvfColorSize(Lock.Listing.Fvf) +
                                     FvfNormalSize(Lock.Listing.Fvf) + FvfPosSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) -
                                                 DestStride * Start);
    Manager->CopyVertexData(Destination, Lock.Listing.Fvf,
                           SubmitContext->SourceVertices, Start,
                           Count);
    const unsigned long TexOffset = FvfColorSize(Lock.Listing.Fvf) + FvfNormalSize(Lock.Listing.Fvf) +
                                    FvfPosSize(Lock.Listing.Fvf);
    uintptr_t DestUv = (uintptr_t)(Lock.Vertices) + TexOffset;

    FTransform ScaleTransform = {};
    ScaleTransform.Rotation.D[0][0] = Scale;
    ScaleTransform.Rotation.D[1][1] = Scale;
    ScaleTransform.Rotation.D[2][2] = Scale;
    FTransform Temporary;
    const FTransform& World = *(const FTransform*)(SubmitContext->World);
    const FTransform& View = *(const FTransform*)(SubmitContext->View);
    if (G_MathEngine == nullptr)
        G_MathEngine = AcquireMathEngine();
    G_MathEngine->MulTransform(Temporary, World, ScaleTransform);
    const FTransform ScaledWorld = Temporary; // own32F0 twelve-dword copy
    int State7 = 1;
    Pipeline->GetPipelineState(7, &State7);
    if (State7 != 0)
    {
        if (G_MathEngine == nullptr)
            G_MathEngine = AcquireMathEngine();
        G_MathEngine->MulTransform(Temporary, View, ScaledWorld);
        const FTransform Modelview = Temporary;
        Pipeline->SetModelview((const float*)(&Modelview));
    }
    else
        Pipeline->SetWorld((const float*)(&ScaledWorld));

    FVector Center = {};
    float Radius = 1.0f;
    if (SubmitContext->Field20 != nullptr)
        ((IEngine*)(SubmitContext->Field20))->GetInstanceExtent(
            SubmitContext->Inst, 0, &Radius, &Center.X);
    const float RadiusOverScale = (float)((double)Radius / Scale);
    if (G_MathEngine == nullptr)
        G_MathEngine = AcquireMathEngine();
    G_MathEngine->MulTransform(Temporary, View, World);
    const FTransform ViewWorld = Temporary;
    const FVector Origin = {};
    FVector Camera;
    if (G_MathEngine == nullptr)
        G_MathEngine = AcquireMathEngine();
    G_MathEngine->InverseTransform(Camera, ViewWorld, Origin);

    // 3082..30C9: X*X + Y*Y, then Z*Z, FSQRT retained before FCOMP.
    const double SquaredXY = (double)(Camera.X) * Camera.X +
                             (double)(Camera.Y) * Camera.Y;
    const double LengthWide = std::sqrt(SquaredXY + (double)(Camera.Z) * Camera.Z);
    float Length = (float)LengthWide;
    // TEST AH,41 + JP keeps greater/unordered; fall-through clamps ordered <=0.
    if (LengthWide <= 0.0)
        Length = AtmosphereEps();
    const double InverseLength = 1.0 / Length;
    const FVector Direction = {
        (float)((double)(Camera.X) * InverseLength),
        (float)((double)(Camera.Y) * InverseLength),
        (float)((double)(Camera.Z) * InverseLength)};
    const float Threshold = (float)(InverseLength * Radius);
    double Shell = ((double)Scale - 1.0) * RadiusOverScale;
    // 311A TEST AH,41 + JP: same ordered <=0 replacement; Shell stays wide.
    if (Shell <= 0.0)
        Shell = AtmosphereEps();
    const double Distance = (double)Length - Radius;
    const double Factor = Distance / (Shell + Distance);
    const float FactorCubed = (float)((Factor * Factor) * Factor);
    double Fade = 1.0 - (double)Alpha;
    // 3182 TEST AH,05 + JP skips equal/greater/unordered; multiply only ordered <.5.
    if ((double)FactorCubed < 0.5)
    {
        Fade *= FactorCubed;
        Fade += Fade;
    }
    uintptr_t NormalAddress = (uintptr_t)(Source->Normal.Base) + Start * 12UL;
    uintptr_t UvCursor = (uintptr_t)(Source->Tex[0].Base) + Start * 8UL;
    const uintptr_t UvEnd = UvCursor + Count * 8UL;
    if ((Source->Flags & 4) == 0)
    {
        while (UvCursor != UvEnd)
        {
            float* Out = (float*)DestUv;
            Out[1] = (float)Fade;
            const float* Normal = (const float*)NormalAddress;
            // 319F..31B5 sums Z*Zdir + Y*Ydir before X*Xdir; retained x87 value.
            const double DotZY = (double)(Direction.Z) * Normal[2] +
                                 (double)(Direction.Y) * Normal[1];
            const double Dot = DotZY + (double)(Direction.X) * Normal[0];
            // TEST AH,05 + JP takes equal/greater/unordered to the formula.
            if (Dot < (double)Threshold)
                Out[0] = 0.0f;
            else
                Out[0] = (float)(((Dot - Threshold) / (1.0 - Threshold)) *
                                             FadeOff * FactorCubed);
            DestUv += DestStride;
            UvCursor += 8;
            NormalAddress += 12;
        }
    }
    Manager->UnlockVertexMemory(&Lock);
    int SavedZFunc;
    Pipeline->GetRenderState(D3DRS_ZFUNC, &SavedZFunc);
    Pipeline->SetRenderState(D3DRS_ZFUNC, 4);
    int SavedZWrite;
    Pipeline->GetRenderState(D3DRS_ZWRITEENABLE, &SavedZWrite);
    Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer, Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(SubmitContext->Indices) + IbStart * 2),
        IndexSpan, 0);
    Pipeline->SetRenderState(D3DRS_ZFUNC, SavedZFunc);
    Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, SavedZWrite);
    // Own DAT_06ed1130 raw words {0x8D,0,0,0}, regardless of mesh marker.
    Pipeline->SetRenderState(D3DRS_COLORVERTEX, 0);
    return GR_OK;
}

GENRESULT FMaterial::SubmitIndexedListing(FMaterialSubmitContext* Context,
                                         unsigned long PrimType, unsigned long Start,
                                         unsigned long Count, unsigned long IbStart,
                                         unsigned long IndexSpan)
{
    // FUN_06eb59a0: only the listing pointer is optional.
    if (Context->VbListing == nullptr)
        return -1;
    return Pipeline->DrawIndexedPrimitiveVb(PrimType, Context->VbListing->VertexBuffer,
        Context->VbListing->BaseVertex + Start, Count,
        (unsigned short*)((uintptr_t)(Context->Indices) + IbStart * 2),
        IndexSpan, 0);
}

GENRESULT FMaterial::SubmitCopiedVertices(FMaterialSubmitContext* Context,
                                         unsigned long PrimType, unsigned long Start,
                                         unsigned long Count, unsigned long IbStart,
                                         unsigned long IndexSpan)
{
    // FUN_06eb59f0: all dependencies are mandatory; only lock failure exits early.
    const FRpVertexSrcDesc* Source = Context->SourceVertices;
    IVertexBufferManager* Manager = VBMgr;
    FVertexMemoryLock Lock;
    const GENRESULT Result = Manager->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Result < 0)
        return Result;
    const unsigned long Stride = FvfPosSize(Lock.Listing.Fvf) + FvfNormalSize(Lock.Listing.Fvf) +
                                 FvfColorSize(Lock.Listing.Fvf) + FvfTexSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) -
                                               Stride * Start);
    Manager->CopyVertexData(Destination, Lock.Listing.Fvf,
                           Context->SourceVertices, Start,
                           Count);
    Manager->UnlockVertexMemory(&Lock);
    return Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer, Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(Context->Indices) + IbStart * 2),
        IndexSpan, 0);
}

GENRESULT FMaterial::SubmitKernel(void* Context, unsigned long PrimType, unsigned long Start,
                                  unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                  unsigned long Flags)
{
    // FUN_06eb3b90: direct draw, indexed listing, then vertex-copy fallback.
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    GENRESULT Result = -1;
    if ((Flags & 0x100) != 0)
    {
        Result = RPDraw->DrawIndexedPrimitive(PrimType, Start, Count,
            SubmitContext->IbScratch->Bias + IbStart, IndexSpan);
        if (Result >= 0)
            return Result;
    }
    Result = SubmitIndexedListing(SubmitContext, PrimType, Start, Count, IbStart, IndexSpan);
    if (Result < 0 && SubmitContext->SourceVertices != nullptr && Count > 0 && IndexSpan > 0)
        Result = SubmitCopiedVertices(SubmitContext, PrimType, Start, Count, IbStart, IndexSpan);
    return Result;
}

GENRESULT FMaterial::SubmitAlphaTest(void* Context, unsigned long PrimType, unsigned long Start,
                                     unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                     unsigned long Flags)
{
    // LAB_06ebda80: alpha scope surrounds the complete FUN_06eb3b90 draw route.
    const bool bSetAlphaTest = bAlphatest != 0 && (Flags & 0x400) == 0;
    int Saved18;
    int Saved19;
    int Saved0F;
    if (bSetAlphaTest)
    {
        Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 0x80);
        Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, 5);
        Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, 1);
    }
    const GENRESULT Result = SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (bSetAlphaTest)
    {
        Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
    }
    return Result;
}

GENRESULT DACOM_API FMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start, unsigned long Count, unsigned long Zero, unsigned long Field, unsigned long Flags)
{
    if (SubmitKind == SubmitNull)
        return GR_OK;
    if (SubmitKind == SubmitStub)
        return GR_OK;
    if (SubmitKind == SubmitAtmosphere)
        return SubmitAtmosphereLock(Context, PrimType, Start, Count, Zero, Field);
    if (SubmitKind == SubmitLightMap)
    {
        // LAB_06eba860: temporary ZFUNC=4; all draw/lock results are discarded.
        int Saved17;
        Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
        Pipeline->SetRenderState(D3DRS_ZFUNC, 4);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
        return GR_OK;
    }
    if (SubmitKind == SubmitIllumDetail2Pass)
    {
        // LAB_06eb9e40: first pass ALPHATEST=0 in either mode.
        int Saved0F;
        Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
        ApplyIllumDetailPassState(0);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        if (DetailPassCount == 1)
        {
            Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
            Teardown(nullptr);
            return GR_OK;
        }
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, 1);
        int Saved18;
        int Saved19;
        int Saved17;
        int Saved0E;
        Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 1);
        Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, 5);
        Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
        Pipeline->SetRenderState(D3DRS_ZFUNC, 4);
        Pipeline->GetRenderState(D3DRS_ZWRITEENABLE, &Saved0E);
        Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        ApplyIllumDetailPassState(1);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, Saved0E);
        Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
        Teardown(nullptr);
        return GR_OK;
    }
    if (SubmitKind == SubmitMaskedDetail2Pass)
    {
        // LAB_06ebbc40: one-pass fallback, or two unconditional kernel draws.
        int Saved0F;
        Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, DetailPassCount == 1 ? 0 : 1);
        if (DetailPassCount == 1)
        {
            ApplyMaskedDetailPassState(0);
            SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
            Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
            Teardown(nullptr);
            return GR_OK;
        }
        int Saved18;
        int Saved19;
        int Saved17;
        Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 0xFE);
        Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, 2);
        ApplyMaskedDetailPassState(0);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 1);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, 5);
        Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
        Pipeline->SetRenderState(D3DRS_ZFUNC, 4);
        ApplyMaskedDetailPassState(1);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
        Teardown(nullptr);
        return GR_OK;
    }
    if (SubmitKind == SubmitDetail2Pass)
    {
        // LAB_06eb7560: mode 1 draws once; mode 2 always draws both passes.
        int Saved0F;
        Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, DetailPassCount == 1 ? 0 : 1);
        if (DetailPassCount == 1)
        {
            ApplyDetailPassState(0);
            SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
            Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
            Teardown(nullptr);
            return GR_OK;
        }
        int Saved18;
        int Saved19;
        int Saved17;
        Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 0xFE);
        Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, 2);
        ApplyDetailPassState(0);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 1);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, 5);
        Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
        Pipeline->SetRenderState(D3DRS_ZFUNC, 4);
        ApplyDetailPassState(1);
        SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        // Scope destructors +28/+34/+10/+1C: ZFUNC, DESTBLEND, SRCBLEND, ALPHATEST.
        Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
        Teardown(nullptr);
        return GR_OK;
    }
    if (SubmitKind == SubmitBtDetailTwo)
    {
        // LAB_06eb55b0: a successful first pass and source descriptor enable pass two.
        const GENRESULT Result = SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
        if (Result < 0 || ((FMaterialSubmitContext*)Context)->SourceVertices == nullptr)
            return Result;
        return SubmitBtDetailTwoSecondPass(Context, PrimType, Start, Count, Zero, Field);
    }
    if (SubmitKind == SubmitDcDt)
        return SubmitAlphaTest(Context, PrimType, Start, Count, Zero, Field, Flags);
    if (SubmitKind == SubmitDcDtTwo)
    {
        // LAB_06ebdfc0: restore first-pass alpha scope before the result/mesh tests.
        const GENRESULT Result =
            SubmitAlphaTest(Context, PrimType, Start, Count, Zero, Field, Flags);
        if (Result < 0 || ((FMaterialSubmitContext*)Context)->SourceVertices == nullptr)
            return Result;
        return SubmitDcDtTwoSecondPass(Context, PrimType, Start, Count, Zero, Field);
    }
    return SubmitKernel(Context, PrimType, Start, Count, Zero, Field, Flags);
}

GENRESULT DACOM_API FMaterial::SetName(const char* Name)
{
    // LAB_06eb1a50: release first, then copy at most 0x40 bytes including NUL.
    delete[] InstanceName;
    InstanceName = nullptr;
    if (Name == nullptr)
        return GR_OK;
    size_t Length = std::strlen(Name) + 1;
    if (Length > 0x40)
        Length = 0x40;
    char* Copy = new char[Length];
    InstanceName = Copy;
    std::strncpy(Copy, Name, Length);
    Copy[Length - 1] = '\0';
    _strlwr(Copy);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetName(char* Out, unsigned long Size)
{
    // LAB_06eb1ac0 RET 0xC: GetName(outBuf, size).
    if (InstanceName == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, InstanceName, Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::CopyClassName(char* Out, unsigned long Size)
{
    if (ClassName() == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, ClassName(), Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetPassCount(int* Out)
{
    // LAB_06eb6e60 reads live +0x74; illum/masked use shared +0x78 accessor.
    *Out = (SubmitKind == SubmitDetail2Pass || SubmitKind == SubmitIllumDetail2Pass ||
            SubmitKind == SubmitMaskedDetail2Pass) ? (int)DetailPassCount : 1;
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetRenderFlags(unsigned long* Out)
{
    if (BindKind == BindAtmosphere)
        *Out = 0xA2;
    else if (BindKind == BindStub)
        *Out = 0x48;
    else if (BindKind == BindNull)
        *Out = 8;
    else if (SubmitKind == SubmitDcDtTwo ||
             (FactoryName != nullptr && std::strcmp(FactoryName, "EcEtTwo") == 0))
        *Out = bAlphablend ? 0x03 : 0x01; // LAB_06ebdfa0
    else if (BindKind == BindDcDtEt)
        *Out = bAlphablend ? 0x4A : 0x48; // LAB_06eb6150
    else if (BindKind == BindNebula)
        *Out = FactoryName != nullptr && std::strcmp(FactoryName, "NebulaTwo") == 0 ?
               0x03UL : 0x0AUL; // c950 / a700: both collapse the alpha branch
    else if (BindKind == BindBtDetail)
    {
        const bool bTwo = FactoryName != nullptr &&
                          std::strcmp(FactoryName, "BtDetailMapTwoMaterial") == 0;
        const unsigned long Base = bTwo ? 0x41UL : 0x48UL;
        *Out = Base | (bAlphablend ? 2UL : 0UL); // LAB_06eb5590 / LAB_06eb6150
    }
    else
        *Out = (BindKind == BindLightMap) ? 0xA : (bAlphablend ? 0xA : 8);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetParameterCount(unsigned long* OutCount)
{
    // LAB_06eb1b50 RET 0x8: slot 27 (PUSH 0) → *OutCount.
    *OutCount = GetParameterTable(nullptr);
    return GR_OK;
}

static void CopyPropertyValue(FShadingPropertyValue* Destination, const FShadingPropertyValue* Source)
{
    // FUN_06eb1d40: capture the tag, then release before reading the live source payload.
    const unsigned long Type = Source->Type;
    Destination->SetType(Type);
    switch (Type)
    {
    case 4:
        Destination->Payload3 = Source->Payload3;
        [[fallthrough]];
    case 3:
        Destination->Payload2 = Source->Payload2;
        Destination->Payload1 = Source->Payload1;
        [[fallthrough]];
    default:
        Destination->Payload0 = Source->Payload0;
        break;
    case 5:
    case 7:
    {
        const char* Text = (const char*)(Source->Payload0);
        if (Destination->Payload0 != 0)
            HeapFree(GetProcessHeap(), 0, (void*)(Destination->Payload0));
        Destination->Payload0 = 0;
        if (Text != nullptr)
        {
            char* Copy = (char*)(HeapAlloc(GetProcessHeap(), 0, std::strlen(Text) + 1));
            Destination->Payload0 = (unsigned long)Copy;
            std::strcpy(Copy, Text);
        }
        break;
    }
    case 6:
    {
        IDAComponent* Component = (IDAComponent*)(Source->Payload0);
        IDAComponent* Previous = (IDAComponent*)(Destination->Payload0);
        Destination->Payload0 = (unsigned long)Component;
        if (Component != nullptr)
            Component->AddRef();
        if (Previous != nullptr)
            Previous->Release();
        break;
    }
    }
}

GENRESULT DACOM_API FMaterial::GetParameterDescriptor(unsigned long Index, void* OutDesc)
{
    // LAB_06eb1b70: index is checked before the mandatory schema/output is used.
    FShadingPropertyRecord* Destination = (FShadingPropertyRecord*)OutDesc;
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Index >= Count)
        return GR_GENERIC;

    const FShadingPropertyRecord* Source = (const FShadingPropertyRecord*)Base + Index;
    Destination->Flags = Source->Flags;
    Destination->Name = Source->Name;
    Destination->Description = Source->Description;
    Destination->Callback = Source->Callback;
    CopyPropertyValue(&Destination->Current, &Source->Current);
    CopyPropertyValue(&Destination->Default, &Source->Default);
    CopyPropertyValue(&Destination->Saved, &Source->Saved);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetParameterValue(unsigned long Index, void* InOutDesc)
{
    FShadingPropertyValue* Result = (FShadingPropertyValue*)InOutDesc;
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Index >= Count)
        return GR_GENERIC;

    FShadingPropertyRecord* EntryRecord = (FShadingPropertyRecord*)Base + Index;
    const unsigned long Type = EntryRecord->Current.Type;
    if (Result->Type != Type)
        Result->ReleaseOwnedPayload();
    Result->Type = Type;

    void* Target = GetParameterAddress(Index);
    if (Target == nullptr)
        return GR_GENERIC;

    switch (Result->Type)
    {
    case 1:
    case 2:
    case 8:
    case 9:
    case 10:
        Result->Payload0 = *(unsigned long*)Target;
        break;
    case 4:
        Result->Payload3 = ((unsigned long*)Target)[3];
        [[fallthrough]];
    case 3:
        Result->Payload2 = ((unsigned long*)Target)[2];
        Result->Payload1 = ((unsigned long*)Target)[1];
        Result->Payload0 = ((unsigned long*)Target)[0];
        break;
    case 5:
    case 7:
    {
        // The retail type-5/7 branch releases an existing same-type string
        // before making a ProcessHeap copy of the NUL-terminated live value.
        const char* Source = *(const char* const*)Target;
        if (Result->Payload0 != 0)
            HeapFree(GetProcessHeap(), 0, (void*)(Result->Payload0));
        Result->Payload0 = 0;
        if (Source != nullptr)
        {
            const size_t Size = std::strlen(Source) + 1;
            char* Copy = (char*)(HeapAlloc(GetProcessHeap(), 0, Size));
            Result->Payload0 = (unsigned long)Copy;
            std::strcpy(Copy, Source);
        }
        break;
    }
    case 6:
    {
        IDAComponent* Component = *(IDAComponent**)Target;
        IDAComponent* Previous = (IDAComponent*)(Result->Payload0);
        Result->Payload0 = (unsigned long)Component;
        if (Component != nullptr)
            Component->AddRef();
        if (Previous != nullptr)
            Previous->Release();
        break;
    }
    default:
        // Retail's default switch arm (caseD_a) still copies the first live
        // dword into the value payload for unknown/future property types.
        Result->Payload0 = *(unsigned long*)Target;
        break;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::SetParameterValue(unsigned long Index, void* Value)
{
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Index >= Count)
        return GR_GENERIC;

    FShadingPropertyRecord* EntryRecord = (FShadingPropertyRecord*)Base + Index;
    if (!ValueWithinDeclaredRange(Index, EntryRecord, Value))
        return GR_GENERIC;

    // 2069 rereads the table output after the virtual verification callback.
    StoreParameterValue(Index, (FShadingPropertyRecord*)Base + Index, Value);
    return GR_OK;
}

const char* __thiscall FMaterial::ClassName()
{
    return ClassNameText != nullptr ? ClassNameText : FactoryName;
}

void* __thiscall FMaterial::ScalarDeletingDestructor(unsigned char bDelete)
{
    void* Storage = this;
    this->~FMaterial();
    // Destruction has already run; the flag selects deallocation only.
    if ((bDelete & 1) != 0)
        ::operator delete(Storage);
    return Storage;
}

GENRESULT __thiscall FMaterial::InitializeFromDescriptor(void* Arg)
{
    // FUN_06eb2b10 RET 0x4: Atmosphere CreateInstance writes byte this+0x74 = 1.
    if (BindKind == BindAtmosphere)
        bAtmosphereReady = 1;
    return GR_OK;
}

GENRESULT __thiscall FMaterial::ApplyInitialRenderStates(void* Arg)
{
    return GR_OK;
}

GENRESULT __thiscall FMaterial::Setup()
{
    // LAB_06eb2090..21f3: the actual slot24 owner of texture verification.
    struct FTextureQuery
    {
        const char* Name;
        unsigned long Crc;
    };
    void* Record; // Retail output scratch is not pre-cleared before Slot12.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        FTexBlock* Block = &TexBlocks[Index];
        ITextureLibrary2* Library = TexLib;
        const int PreviousId = Block->TextureId;
        Block->Handle = 0;
        Block->TextureId = -1;
        Block->bSkipResolve = 1;
        bool bSuppressNotice = true;
        if (Block->Name != nullptr)
        {
            FTextureQuery Query;
            Query.Name = Block->Name;
            Query.Crc = DACOM_CRC::GetCRC32(Block->Name);
            if (Library->FindTextureByCrc(&Query, &Record) >= 0)
            {
                int FrameCount;
                Library->GetFrameCount(Record, &FrameCount);
                Block->bSkipResolve = FrameCount == 1 ? 1 : 0;
                if (Library->CreatePlaybackGroup(Record, &Block->TextureId) >= 0 &&
                    Block->bSkipResolve != 0)
                {
                    Library->GetFrame(Block->TextureId, 0, &Block->Handle);
                }
                Library->ReleaseTextureRecord(Record);
            }
            else
            {
                // Retail compares exactly seven bytes, including the terminating NUL.
                bSuppressNotice = std::strncmp(Block->Name, "(None)", 7) == 0;
            }
        }
        if (PreviousId != -1)
            Library->DecRefById(PreviousId);
        if (!bSuppressNotice)
            FDUMP(0x100003, "%s(%d) : NOTICE:General:texture verify failed: %s: %s",
                  __FILE__, __LINE__, InstanceName, TexBlocks[Index].Name);
    }
    // TEST AH,0x05 / JP takes the zero side for greater/equal/unordered.
    // Only ordered Opacity < 1 falls through to blend=1.
    bAlphablend = Opacity < 1.0f ? 1 : 0;
    bAlphatest = 0;
    if (bAlphablend != 0)
        return GR_OK;
    int FormatIndex = 0; // FUN_06ed0b60(0), fixed DAT_06ed51d8 first entry.
    const unsigned long Handle = TexBlocks[0].Handle;
    if (Handle == 0)
        return GR_OK;
    if (Pipeline->GetTextureFormat((void*)Handle, &FormatIndex) < 0)
        return GR_OK;
    bAlphatest = PixelFormatAlphaBits(FormatIndex) == 1 ? 1 : 0;
    const bool bHasAlpha = (FormatIndex >= 10 && FormatIndex <= 14) ||
                           PixelFormatHasAlpha(FormatIndex) != 0;
    bAlphablend = bHasAlpha && bAlphatest == 0 ? 1 : 0;
    return GR_OK;
}

static float ShadingPropertyFloat(unsigned long Bits)
{
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static bool IsShadingPropertyValueWithinRange(unsigned long ValueBits, unsigned long MinimumBits,
                                              unsigned long MaximumBits)
{
    // Compare widened bounds against the stored float without rounding the bounds.
    static const unsigned long kRangeEpsilonBits = 0x34000000;
    const double Value = ShadingPropertyFloat(ValueBits);
    const double Epsilon = ShadingPropertyFloat(kRangeEpsilonBits);
    const double Upper = (double)(ShadingPropertyFloat(MaximumBits)) + Epsilon;
    if (Upper <= Value)
        return false;
    const double Lower = (double)(ShadingPropertyFloat(MinimumBits)) - Epsilon;
    return !(Lower >= Value);
}

bool __thiscall FMaterial::ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value)
{
    // Require matching value types before checking the declared range.
    const FShadingPropertyRecord* Record = (const FShadingPropertyRecord*)Entry;
    const FShadingPropertyValue* Typed = (const FShadingPropertyValue*)Value;
    if (Typed->Type != Record->Current.Type)
        return false;

    if (Typed->Type == 2)
    {
        return IsShadingPropertyValueWithinRange(Typed->Payload0, Record->Default.Payload0,
                                                 Record->Saved.Payload0);
    }
    if (Typed->Type == 3 || Typed->Type == 4)
    {
        const unsigned long ComponentCount = Typed->Type == 3 ? 3UL : 4UL;
        const unsigned long* ValuePayload = &Typed->Payload0;
        const unsigned long* MinimumPayload = &Record->Default.Payload0;
        const unsigned long* MaximumPayload = &Record->Saved.Payload0;
        for (unsigned long ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
        {
            if (!IsShadingPropertyValueWithinRange(ValuePayload[ComponentIndex],
                                                   MinimumPayload[ComponentIndex],
                                                   MaximumPayload[ComponentIndex]))
            {
                return false;
            }
        }
    }
    return true;
}

GENRESULT __thiscall FMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    // FUN_06eb23c0: the target guard covers typed copy only.
    const FShadingPropertyValue* Typed = (const FShadingPropertyValue*)Value;
    const FShadingPropertyRecord* Record = (const FShadingPropertyRecord*)Entry;
    void* Target = GetParameterAddress(Index);
    if (Target != nullptr)
    {
        unsigned long* Words = (unsigned long*)Target;
        switch (Typed->Type)
        {
        case 4:
            Words[3] = Typed->Payload3;
            [[fallthrough]];
        case 3:
            Words[2] = Typed->Payload2;
            Words[1] = Typed->Payload1;
            [[fallthrough]];
        case 1:
        case 2:
        case 8:
        case 9:
        case 10:
            Words[0] = Typed->Payload0;
            break;
        case 5:
        case 7:
        {
            char** TextTarget = (char**)Target;
            delete[] *TextTarget;
            *TextTarget = nullptr;
            const char* Source = (const char*)(Typed->Payload0);
            if (Source != nullptr)
            {
                const size_t Size = std::strlen(Source) + 1;
                char* Copy = new char[Size];
                *TextTarget = Copy;
                // 248a rereads the value after publishing the allocated target.
                std::strcpy(Copy, (const char*)(Typed->Payload0));
            }
            break;
        }
        case 6:
        {
            IDAComponent** ComponentTarget = (IDAComponent**)Target;
            IDAComponent* Previous = *ComponentTarget;
            IDAComponent* Component = (IDAComponent*)(Typed->Payload0);
            *ComponentTarget = Component;
            if (Component != nullptr)
                Component->AddRef();
            if (Previous != nullptr)
                Previous->Release();
            break;
        }
        default:
            // 23df..23e3: tags outside 1..10 skip copying entirely.
            break;
        }
    }
    if (std::strcmp(Record->Name, "Dc") == 0)
        std::memcpy(Ambient, &DiffuseR, 4 * sizeof(float));
    else if (std::strcmp(Record->Name, "Oc") == 0)
        ApplyOcFlag();
    if (SubmitKind == SubmitDetail2Pass || BindKind == BindDetailMap ||
        SubmitKind == SubmitIllumDetail2Pass || SubmitKind == SubmitMaskedDetail2Pass)
    {
        // FUN_06eb6c40 / FUN_06ebb3f0 tail after every name-keyed post-effect.
        if (Index == 6)
            DetailFlipU = (int)DetailFlipU > 0 ? 1UL : 0UL;
        else if (Index == 7)
            DetailFlipV = (int)DetailFlipV > 0 ? 1UL : 0UL;
    }
    return GR_OK;
}

unsigned long __thiscall FMaterial::GetParameterTable(void** OutBase)
{
    if (BindKind == BindNebula)
    {
        InitializeNebulaPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GNebulaPropertySchema;
        return 4;
    }
    if (BindKind == BindLightMap)
    {
        InitializeLightMapPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GLightMapPropertySchema;
        return 4;
    }

    if (BindKind == BindAtmosphere)
    {
        // LAB_06eb25c0 / 06eb28ce: optional OutBase receives DAT_06ed7ca0,
        // then the function returns exactly 9.
        FShadingPropertyRecord* Schema = GetAtmospherePropertySchema();
        if (OutBase != nullptr)
            *OutBase = Schema;
        return 9;
    }
    if (IsSinglePassPropertySchemaFamily(FactoryName))
    {
        FShadingPropertyRecord* Schema = GetSinglePassPropertySchema();
        if (OutBase != nullptr)
            *OutBase = Schema;
        return 10;
    }
    if (BindKind == BindEcEt)
    {
        FShadingPropertyRecord* Schema = GetEcEtPropertySchema();
        if (OutBase != nullptr)
            *OutBase = Schema;
        return 5;
    }
    if (BindKind == BindDcDtEt)
    {
        FShadingPropertyRecord* Schema = GetDcDtEtPropertySchema();
        if (OutBase != nullptr)
            *OutBase = Schema;
        return 9;
    }
    if (BindKind == BindBtDetail)
    {
        InitializeBtDetailPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GBtDetailPropertySchema;
        return 13;
    }
    if (BindKind == BindDetailMap)
    {
        InitializeDetailMapPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GDetailMapPropertySchema;
        return 13;
    }
    if (SubmitKind == SubmitDetail2Pass)
    {
        InitializeDetail2PassPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GDetail2PassPropertySchema;
        return 13;
    }
    if (SubmitKind == SubmitIllumDetail2Pass)
    {
        InitializeIllumDetailPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GIllumDetailPropertySchema;
        return 18;
    }
    if (SubmitKind == SubmitMaskedDetail2Pass)
    {
        InitializeMaskedDetailPropertySchema();
        if (OutBase != nullptr)
            *OutBase = GMaskedDetailPropertySchema;
        return 18;
    }
    if (OutBase != nullptr)
        *OutBase = nullptr;
    return 0; // LAB_06eb3da0: NullMaterial has no properties.
}

void* __thiscall FMaterial::GetParameterAddress(unsigned long Index)
{
    // FUN_06eb1a00 is NullMaterial slot 28: always return null.
    if (BindKind == BindNull)
        return nullptr;

    if (BindKind == BindNebula || BindKind == BindLightMap)
    {
        // LAB_06ebc5a0: actual texture block, not fields of the material itself.
        switch (Index)
        {
        case 0: return &TexBlocks[0].Name;
        case 1: return &TexBlocks[0].Flags;
        case 2: return &TexBlocks[0].AddressU;
        case 3: return &TexBlocks[0].AddressV;
        default: return nullptr;
        }
    }

    if (BindKind == BindAtmosphere)
    {
        switch (Index)
        {
        case 0:
            return Ambient;
        case 1:
            return &DiffuseR;
        case 2:
            return &TexBlocks[0].Name;
        case 3:
            return &TexBlocks[0].Flags;
        case 4:
            return &TexBlocks[0].AddressU;
        case 5:
            return &TexBlocks[0].AddressV;
        case 6:
            return &FadeOff;
        case 7:
            return &Alpha;
        case 8:
            return &Scale;
        default:
            return nullptr;
        }
    }
    if (BindKind == BindEcEt)
    {
        // LAB_06eb89e0: EcEt's five-record table addresses Emissive at
        // +0x50 and the single Et texture block at +0x18/+1C/+20/+24.
        switch (Index)
        {
        case 0:
            return Emissive;
        case 1:
            return (char**)(&TexBlocks[0].Name);
        case 2:
            return &TexBlocks[0].Flags;
        case 3:
            return &TexBlocks[0].AddressU;
        case 4:
            return &TexBlocks[0].AddressV;
        default:
            return nullptr;
        }
    }
    if (BindKind == BindDcDtEt)
    {
        // LAB_06eb5dd0's nine records map Ac, then the diffuse and emissive
        // texture name/flags/address fields in the two embedded blocks.
        switch (Index)
        {
        case 0:
            return &DiffuseR;
        case 1:
            return (char**)(&TexBlocks[0].Name);
        case 2:
            return &TexBlocks[0].Flags;
        case 3:
            return &TexBlocks[0].AddressU;
        case 4:
            return &TexBlocks[0].AddressV;
        case 5:
            return (char**)(&TexBlocks[1].Name);
        case 6:
            return &TexBlocks[1].Flags;
        case 7:
            return &TexBlocks[1].AddressU;
        case 8:
            return &TexBlocks[1].AddressV;
        default:
            return nullptr;
        }
    }
    if (BindKind == BindBtDetail)
    {
        switch (Index)
        {
        case 0: return Ambient;
        case 1: return &DiffuseR;
        case 2: return Emissive;
        case 3: return Specular;
        case 4: return &SpecularPower;
        case 5: return &Opacity;
        case 6: return &TexBlocks[0].Name;
        case 7: return &TexBlocks[0].Flags;
        case 8: return &TexBlocks[0].AddressU;
        case 9: return &TexBlocks[0].AddressV;
        case 10: return &TexBlocks[1].Name;
        case 11: return &TexBlocks[1].Flags;
        case 12: return &TexBlocks[1].AddressU;
        case 13: return &TexBlocks[1].AddressV;
        default: return nullptr;
        }
    }
    // LAB_06eb6b80 is shared by DetailMapMaterial and DetailMap2Dm1Msk2Pass.
    if (SubmitKind == SubmitDetail2Pass || BindKind == BindDetailMap)
    {
        switch (Index)
        {
        case 0: return Ambient;
        case 1: return &DiffuseR;
        case 2: return &TexBlocks[0].Name;
        case 3: return &TexBlocks[0].Flags;
        case 4: return &TexBlocks[0].AddressU;
        case 5: return &TexBlocks[0].AddressV;
        case 6: return &DetailFlipU;
        case 7: return &DetailFlipV;
        case 8: return &TexBlocks[1].Name;
        case 9: return &TexBlocks[1].Flags;
        case 10: return &TexBlocks[1].AddressU;
        case 11: return &TexBlocks[1].AddressV;
        case 12: return &DetailTileRate0;
        default: return nullptr;
        }
    }
    // LAB_06ebb2e0: all 18 targets, including the distinct third texture block.
    if (SubmitKind == SubmitIllumDetail2Pass || SubmitKind == SubmitMaskedDetail2Pass)
    {
        switch (Index)
        {
        case 0:
            return Ambient;
        case 1:
            return &DiffuseR;
        case 2:
            return &TexBlocks[0].Name;
        case 3:
            return &TexBlocks[0].Flags;
        case 4:
            return &TexBlocks[0].AddressU;
        case 5:
            return &TexBlocks[0].AddressV;
        case 6:
            return &DetailFlipU;
        case 7:
            return &DetailFlipV;
        case 8:
            return &TexBlocks[1].Name;
        case 9:
            return &TexBlocks[1].Flags;
        case 10:
            return &TexBlocks[1].AddressU;
        case 11:
            return &TexBlocks[1].AddressV;
        case 12:
            return &DetailTileRate0;
        case 13:
            return &TexBlocks[2].Name;
        case 14:
            return &TexBlocks[2].Flags;
        case 15:
            return &TexBlocks[2].AddressU;
        case 16:
            return &TexBlocks[2].AddressV;
        case 17:
            return &DetailTileRate1;
        default:
            return nullptr;
        }
    }
    // LAB_06ebd770: ten SinglePass targets; unsigned out-of-range returns null.
    switch (Index)
    {
    case 0:
        return Ambient;
    case 1:
        return &DiffuseR;
    case 2:
        return Emissive;
    case 3:
        return Specular;
    case 4:
        return &SpecularPower;
    case 5:
        return &Opacity;
    case 6:
        return (char**)(&TexBlocks[0].Name);
    case 7:
        return &TexBlocks[0].Flags;
    case 8:
        return &TexBlocks[0].AddressU;
    case 9:
        return &TexBlocks[0].AddressV;
    default:
        return nullptr;
    }
}

void FMaterial::ApplyOcFlag()
{
    // FUN_06eb6c40 / FUN_06eb23c0 name "Oc". Listing 06eb6d63–06eb6dfc.
    const bool bOpacityBlend = (Opacity < 1.0f);
    bAlphablend = bOpacityBlend ? 1 : 0;
    bAlphatest = 0;
    if (bOpacityBlend)
        return;
    const unsigned long Handle = TexBlocks[0].Handle;
    if (Handle == 0)
        return;
    // FUN_06ed0b60(0) → index of DAT_06ed51d8 value 0 = 0. Pipeline +0xC8 overwrites.
    int FormatIndex = 0;
    const GENRESULT Result = Pipeline->GetTextureFormat((void*)Handle, &FormatIndex);
    if (Result < 0)
        return;
    bAlphatest = (PixelFormatAlphaBits(FormatIndex) == 1) ? 1 : 0;
    const bool bDxtRange =
        (FormatIndex >= kDxtFormatIndexMin && FormatIndex <= kDxtFormatIndexMax);
    const bool bHasAlpha = bDxtRange || (PixelFormatHasAlpha(FormatIndex) != 0);
    if (bHasAlpha && bAlphatest == 0)
        bAlphablend = 1;
    else
        bAlphablend = 0;
}

FBatchedFace::FBatchedFace() {}

FBatchedMaterial* FBatchedFace::OwnerFromThis()
{
    return (FBatchedMaterial*)((unsigned char*)this -
                                               offsetof(FBatchedMaterial, Batched));
}

GENRESULT DACOM_API FBatchedFace::QueryInterface(const char* Iid, void** Out)
{
    FBatchedMaterial* Owner = OwnerFromThis();
    if (Owner == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FBatchedFace::AddRef()
{
    FBatchedMaterial* Owner = OwnerFromThis();
    return Owner->AddRef();
}

unsigned long DACOM_API FBatchedFace::Release()
{
    FBatchedMaterial* Owner = OwnerFromThis();
    return Owner->Release();
}

GENRESULT DACOM_API FBatchedFace::CreateInnerMaterial(void* Name)
{
    // LAB_06eb4380: retained controller and borrowed public material are distinct.
    FBatchedMaterial* Owner = OwnerFromThis();
    if (Owner->InnerController != nullptr)
    {
        Owner->InnerController->Release();
        Owner->InnerController = nullptr;
    }
    FMaterialAggregateDesc Desc = {};
    Desc.Size = sizeof(Desc);
    Desc.NameAt4 = (const char*)Name;
    Desc.OuterAt8 = (IBatchedMaterial*)this;
    Desc.InnerOutAtC = (void**)(&Owner->InnerController);
    Desc.ExtraAt10 = nullptr;
    void* Created = nullptr;
    const GENRESULT Result = DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc), &Created);
    if (Result >= 0)
    {
        if (Owner->InnerController->QueryInterface(Iids::Material,
                                                   (void**)(&Owner->Inner)) >= 0)
        {
            // Public QI added a reference to this wrapper; retain only inner controller.
            Owner->Inner->Release();
        }
        else if (Created != nullptr)
        {
            ((IDAComponent*)Created)->Release();
        }
    }
    else
    {
        char Message[8192];
        std::sprintf(Message, "Batched material references invalid super type (%s)\n",
                     (const char*)Name);
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\BatchedMaterial.cpp", 0x159,
              Message);
    }
    return GR_OK;
}

GENRESULT DACOM_API FBatchedFace::SetBatchBin(void* Value)
{
    FBatchedMaterial* Owner = OwnerFromThis();
    Owner->Field8 = (unsigned long)Value;
    return GR_OK;
}

GENRESULT DACOM_API FBatchedFace::GetBatchBin(void** Out)
{
    FBatchedMaterial* Owner = OwnerFromThis();
    *Out = (void*)(Owner->Field8);
    return GR_OK;
}

GENRESULT DACOM_API FBatchedFace::Bind(void* Context)
{
    IMaterial* Inner = OwnerFromThis() != nullptr ? OwnerFromThis()->InnerMaterial() : nullptr;
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Bind(Context);
}

GENRESULT DACOM_API FBatchedFace::Teardown(void* Context)
{
    IMaterial* Inner = OwnerFromThis() != nullptr ? OwnerFromThis()->InnerMaterial() : nullptr;
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Teardown(Context);
}

GENRESULT DACOM_API FBatchedFace::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                        unsigned long Count, unsigned long Zero, unsigned long Field,
                                        unsigned long Flags)
{
    IMaterial* Inner = OwnerFromThis()->InnerMaterial();
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Submit(Context, PrimType, Start, Count, Zero, Field, Flags);
}

FBatchedMaterial::FBatchedMaterial()
    : Field8(0xFFFFFFFF), InnerController(nullptr), Inner(nullptr), Batcher(nullptr), RefCount(1), Outer(nullptr)
{
}

FBatchedMaterial::~FBatchedMaterial() noexcept(false)
{
    // FUN_06eb3e50: release the factory controller, then clear the borrowed face.
    if (InnerController != nullptr)
    {
        InnerController->Release();
        InnerController = nullptr;
    }
    Inner = nullptr;
}

IMaterial* FBatchedMaterial::InnerMaterial() const
{
    return Inner;
}

static_assert(offsetof(FBatchedMaterial, Batched) == 0x04, "IBatchedMaterial +4");
static_assert(offsetof(FBatchedMaterial, Field8) == 0x08, "Field8 +8");
static_assert(offsetof(FBatchedMaterial, InnerController) == 0x0C, "retained controller +C");
static_assert(offsetof(FBatchedMaterial, Inner) == 0x10, "inner IMaterial +10");
static_assert(offsetof(FBatchedMaterial, Batcher) == 0x14, "batcher +14");

FBatchedMaterial* FBatchedController::OwnerFromThis()
{
    return (FBatchedMaterial*)((unsigned char*)this -
                                               offsetof(FBatchedMaterial, Controller));
}

GENRESULT DACOM_API FBatchedController::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06eb4640: native direct entries mirror table06ed1320 offsets0 and4.
    FBatchedMaterial* Owner = OwnerFromThis();
    IDAComponent* Face;
    if (std::strcmp(Iid, Iids::Material) == 0)
        Face = (IMaterial*)Owner;
    else if (std::strcmp(Iid, Iids::BatchedMaterial) == 0)
        Face = (IBatchedMaterial*)(&Owner->Batched);
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

unsigned long DACOM_API FBatchedController::AddRef()
{
    return ++OwnerFromThis()->RefCount;
}

unsigned long DACOM_API FBatchedController::Release()
{
    FBatchedMaterial* Owner = OwnerFromThis();
    if (Owner->RefCount != 0)
        --Owner->RefCount;
    const unsigned long Remaining = Owner->RefCount;
    if (Remaining == 0)
    {
        Owner->RefCount = 1;
        void* Storage = Owner;
        Owner->~FBatchedMaterial();
        // The destructor has already run; release only its storage.
        ::operator delete(Storage);
    }
    return Remaining;
}

GENRESULT FBatchedMaterial::InitializeFromDescriptor(FMaterialAggregateDesc* Desc)
{
    // FUN_06eb3e80, also called by factory4530 after controller publication.
    InnerController = nullptr;
    Inner = nullptr;
    Field8 = 0xFFFFFFFFu;
    return GR_OK;
}

void FBatchedMaterial::ConfigureAggregation(FMaterialAggregateDesc* Desc)
{
    Outer = Desc->OuterAt8;
    if (Outer != nullptr)
        *Desc->InnerOutAtC = (IDAComponent*)(&Controller);
}

GENRESULT DACOM_API FBatchedMaterial::QueryInterface(const char* Iid, void** Out)
{
    return Outer != nullptr ? Outer->QueryInterface(Iid, Out) : Controller.QueryInterface(Iid, Out);
}

unsigned long DACOM_API FBatchedMaterial::AddRef()
{
    return Outer != nullptr ? Outer->AddRef() : Controller.AddRef();
}

unsigned long DACOM_API FBatchedMaterial::Release()
{
    return Outer != nullptr ? Outer->Release() : Controller.Release();
}

GENRESULT DACOM_API FBatchedMaterial::InnerQueryInterface(const char* Iid, void** Out)
{
    return Controller.QueryInterface(Iid, Out);
}
unsigned long DACOM_API FBatchedMaterial::InnerAddRef()
{
    return Controller.AddRef();
}
unsigned long DACOM_API FBatchedMaterial::InnerRelease()
{
    return Controller.Release();
}

GENRESULT DACOM_API FBatchedMaterial::Initialize(IDAComponent* Component)
{
    // FUN_06eb3ea0: direct out-parameter writes persist even on failed QI.
    if (Component->QueryInterface(Iids::MaterialBatcher, &Batcher) < 0)
    {
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\BatchedMaterial.cpp", 0x77,
              "BatchedMaterial: initialize: unable to acquire IID_IMaterialBatcher");
        return GR_GENERIC;
    }
    ((IDAComponent*)Batcher)->Release();
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Initialize(Component);
}

GENRESULT DACOM_API FBatchedMaterial::LoadFromFileSystem(void* FileSystem)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    if (!ReadNamedBytes(FileSystem, "Bin", &Field8, sizeof(Field8)))
        Field8 = 0xFFFFFFFF;
    return Inner->LoadFromFileSystem(FileSystem);
}

GENRESULT DACOM_API FBatchedMaterial::SaveProperties(void* FileSystem)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    if (!WriteNamedBytes(FileSystem, "Bin", &Field8, sizeof(Field8)))
        Field8 = 0xFFFFFFFF;
    return Inner->SaveProperties(FileSystem);
}

GENRESULT DACOM_API FBatchedMaterial::ApplyVerify(void* Context, void* Extra)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    const GENRESULT Result = Inner->ApplyVerify(Context, Extra);
    // FUN_06eb40f0 caches bit 1 of the low render-flags byte after the first
    // successful verification.  Field8==-1 is the one-shot sentinel.
    if (Field8 == 0xFFFFFFFF && Result >= 0)
    {
        unsigned long Flags = 0;
        Inner->GetRenderFlags(&Flags);
        Field8 = ((unsigned char)Flags >> 1) & 1;
    }
    return Result;
}

GENRESULT DACOM_API FBatchedMaterial::ResolveTextures(float Delta)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->ResolveTextures(Delta);
}

GENRESULT DACOM_API FBatchedMaterial::Bind(void* Context)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Bind(Context);
}

GENRESULT DACOM_API FBatchedMaterial::Teardown(void* Context)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Teardown(Context);
}

GENRESULT DACOM_API FBatchedMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start, unsigned long Count, unsigned long Zero, unsigned long Field, unsigned long Flags)
{
    // LAB_06eb4180: only the inner material is optional; the batcher is initialized.
    if (Inner == nullptr)
        return GR_GENERIC;

    ((IMaterialBatcher2*)Batcher)->SelectBin(Field8);

    unsigned long DepthBits = 0;
    if ((Flags & 0x40) != 0)
    {
        // Capture both pointers before the lazy factory callback, as at 41bd..41d0.
        const FMaterialSubmitContext* SubmitContext = (const FMaterialSubmitContext*)Context;
        const FTransform* View = (const FTransform*)(SubmitContext->View);
        const FVector* Translation = &((const FTransform*)(SubmitContext->World))->Translation;
        static I3DMathEngine* GBatchedMathEngine = nullptr; // retail DAT_06ed56c8
        Flags |= 0x20;
        if (GBatchedMathEngine == nullptr)
            GBatchedMathEngine = AcquireMathEngine();
        FVector Transformed;
        GBatchedMathEngine->TransformXform(Transformed, *View, *Translation);
        const float Depth = -Transformed.Z; // 41f2: load the stored third float, then FCHS.
        std::memcpy(&DepthBits, &Depth, sizeof(Depth));
    }
    // 4201/4221 reread the batcher and inner after the foreign math callback.
    return ((IMaterialBatcher2*)Batcher)->Submit(Inner, Context, PrimType, Start, Count, Zero, Field,
                                                         (void*)DepthBits, Flags);
}

GENRESULT DACOM_API FBatchedMaterial::SetName(const char* Name)
{
    // Forwards like the sibling slots above (Slot5/ApplyVerify/.../CopyName).
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->SetName(Name);
}

GENRESULT DACOM_API FBatchedMaterial::GetName(char* Out, unsigned long Size)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetName(Out, Size);
}

GENRESULT DACOM_API FBatchedMaterial::CopyClassName(char* Out, unsigned long Size)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->CopyClassName(Out, Size);
}

GENRESULT DACOM_API FBatchedMaterial::GetPassCount(int* Out)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetPassCount(Out);
}

GENRESULT DACOM_API FBatchedMaterial::GetRenderFlags(unsigned long* Out)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    unsigned long Value = 0;
    if (Inner->GetRenderFlags(&Value) < GR_OK)
        return GR_GENERIC;
    *Out = Value | 0x10;
    return GR_OK;
}

GENRESULT DACOM_API FBatchedMaterial::GetParameterCount(unsigned long* OutCount)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetParameterCount(OutCount);
}

GENRESULT DACOM_API FBatchedMaterial::GetParameterDescriptor(unsigned long Index, void* OutDesc)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetParameterDescriptor(Index, OutDesc);
}

GENRESULT DACOM_API FBatchedMaterial::GetParameterValue(unsigned long Index, void* InOutDesc)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetParameterValue(Index, InOutDesc);
}

GENRESULT DACOM_API FBatchedMaterial::SetParameterValue(unsigned long Index, void* Value)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->SetParameterValue(Index, Value);
}

const char* __thiscall FBatchedMaterial::ClassName()
{
    if (Inner != nullptr)
        return Inner->ClassName();
    return "BatchedMaterial";
}

void* __thiscall FBatchedMaterial::ScalarDeletingDestructor(unsigned char bDelete)
{
    if ((bDelete & 1) != 0)
    {
        delete this;
        return nullptr;
    }
    return this;
}

GENRESULT __thiscall FBatchedMaterial::InitializeFromDescriptor(void* Arg)
{
    return GR_OK;
}

GENRESULT __thiscall FBatchedMaterial::ApplyInitialRenderStates(void* Arg)
{
    return GR_OK;
}

GENRESULT __thiscall FBatchedMaterial::Setup()
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Setup();
}

bool __thiscall FBatchedMaterial::ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value)
{
    if (Inner == nullptr)
        return false;
    return Inner->ValueWithinDeclaredRange(Index, Entry, Value);
}

GENRESULT __thiscall FBatchedMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->StoreParameterValue(Index, Entry, Value);
}

unsigned long __thiscall FBatchedMaterial::GetParameterTable(void** OutBase)
{
    if (OutBase != nullptr)
        *OutBase = nullptr;
    return 0;
}

void* __thiscall FBatchedMaterial::GetParameterAddress(unsigned long Index)
{
    return nullptr;
}

struct FMaterialTypeSpec
{
    const char* FactoryName;
    const char* ClassName;
    EMaterialBind BindKind;
    EMaterialSubmit SubmitKind;
};

static const FMaterialTypeSpec kMaterialTypes[] = {
    {"DcDt", "SinglePassMaterial", BindDcDt, SubmitDcDt},
    {"DcDtEc", "SinglePassMaterial", BindDcDt, SubmitDcDt},
    {"DcDtOcOt", "SinglePassMaterial", BindDcDt, SubmitDcDt},
    {"DcDtEcOcOt", "SinglePassMaterial", BindDcDt, SubmitDcDt},
    {"DcDtTwo", "SinglePassTwoMaterial", BindDcDt, SubmitDcDtTwo},
    {"DcDtEcTwo", "SinglePassTwoMaterial", BindDcDt, SubmitDcDtTwo},
    {"DcDtOcOtTwo", "SinglePassTwoMaterial", BindDcDt, SubmitDcDtTwo},
    {"DcDtEcOcOtTwo", "SinglePassTwoMaterial", BindDcDt, SubmitDcDtTwo},
    {"DcDtEt", "DcDtEt", BindDcDtEt, SubmitKernel},
    {"EcEt", "EcEt", BindEcEt, SubmitKernel},
    {"EcEtTwo", "EcEtTwo", BindEcEt, SubmitKernel},
    {"AtmosphereMaterial", "AtmosphereMaterial", BindAtmosphere, SubmitAtmosphere},
    {"Nebula", "Nebula", BindNebula, SubmitKernel},
    {"NebulaTwo", "NebulaTwo", BindNebula, SubmitKernel},
    {"NullMaterial", "NullMaterial", BindNull, SubmitNull},
    {"LightMap", "LightMap", BindLightMap, SubmitLightMap},
    {"DetailMapMaterial", "DetailMapMaterial", BindDetailMap, SubmitKernel},
    {"DetailMap2Dm1Msk2PassMaterial", "DetailMap2Dm1Msk2PassMaterial", BindStub, SubmitDetail2Pass},
    {"IllumDetailMapMaterial", "IllumDetailMapMaterial", BindStub, SubmitIllumDetail2Pass},
    {"Masked2DetailMapMaterial", "Masked2DetailMapMaterial", BindStub, SubmitMaskedDetail2Pass},
    {"BtDetailMapMaterial", "BtDetailMapMaterial", BindBtDetail, SubmitKernel},
    {"BtDetailMapTwoMaterial", "BtDetailMapTwoMaterial", BindBtDetail, SubmitBtDetailTwo},
};

FMaterial* CreateMaterialInstance(const char* FactoryName)
{
    if (FactoryName == nullptr)
        return nullptr;
    const int Count = (int)(sizeof(kMaterialTypes) / sizeof(kMaterialTypes[0]));
    for (int Index = 0; Index < Count; ++Index)
    {
        if (std::strcmp(kMaterialTypes[Index].FactoryName, FactoryName) == 0)
        {
            FMaterial* Material = new (std::nothrow)
                FMaterial(kMaterialTypes[Index].FactoryName, kMaterialTypes[Index].ClassName,
                          kMaterialTypes[Index].BindKind, kMaterialTypes[Index].SubmitKind);
            return Material;
        }
    }
    return nullptr;
}

FMaterial* AsShadingFMaterial(IMaterial* Material)
{
    if (Material == nullptr)
        return nullptr;
    void* Batched = nullptr;
    if (Material->QueryInterface(Iids::BatchedMaterial, &Batched) == GR_OK && Batched != nullptr)
    {
        ((IDAComponent*)Batched)->Release();
        return nullptr;
    }
    const char* ClassName = Material->ClassName();
    if (ClassName == nullptr)
        return nullptr;
    const int Count = (int)(sizeof(kMaterialTypes) / sizeof(kMaterialTypes[0]));
    for (int Index = 0; Index < Count; ++Index)
    {
        if (std::strcmp(kMaterialTypes[Index].ClassName, ClassName) == 0 ||
            std::strcmp(kMaterialTypes[Index].FactoryName, ClassName) == 0)
            return (FMaterial*)Material;
    }
    return nullptr;
}
